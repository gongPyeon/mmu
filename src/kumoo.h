#define ADDR_SIZE 16 //address size
#define PAGE_SIZE 64
#define PFNUM 4096 // 2^12
#define SFNUM 16384 //2^14
#define VPN_SHIFT 6
#define PFN_SHIFT 4
#define SHIFT 5
#define VPN_MASK 0xFFC0
#define OFFSET_MASK 0x003F
#define PD_MASK 0x03E0
#define PT_MASK 0x001F
#define PFN_MASK 0xFFC0

#include<string.h>

struct pcb *current; // 현재 실행하고 있는 pcb를 가리키는 포인터
unsigned short *pdbr; //page directory base주소를 가리키는 포인터
unsigned short *ptbr; // page table base 주소를 가리키는 포인터
char *pmem, *swaps; //각각 물리 메모리와 swap공간에 대한 base주소를 가리키는 포인터
int pfnum, sfnum; //각각 page frame의 개수
char* pmem_free, *swaps_free; //물리 메모리의 free list
int pidCount = 0; // pid개수

void ku_dump_pmem(void);
void ku_dump_swap(void);

struct pcb{
    int base, bound;
    unsigned short pid;
    FILE *fd;
    unsigned short *pgdir; //page directory에 대한 주소
};

// pcb process list
typedef struct pcbNode{
	struct pcbNode *next;
	struct pcb pcblock;
}pcbNode;
pcbNode *processList = NULL;

//swap in page list
typedef struct swapInPageNode{ 
    int pid; //해당 페이지의 process pid
    char* pteAddress; //해당 페이지 정보가 담겨있는 pte 주소
    struct swapInPageNode *next;
} swapInPageNode;
swapInPageNode *swapInList = NULL;


void ku_freelist_init(void){
    //free list를 초기화한다
    pfnum = PFNUM;
    sfnum = SFNUM;

    pmem_free = malloc(PFNUM);
    for(int i=0; i<PFNUM; i++){
        pmem_free[i] = 0;
    }

    swaps_free = malloc(SFNUM);
     for(int i=0; i<SFNUM; i++){
        swaps_free[i] = 0;
    }

    for(int i = 0; i<PFNUM*64; i++){
        pmem[i] = 0;
    }
    //phy는 0부터 시작한다
    for(int i = 0; i<SFNUM*64; i++){
        swaps[i] = 0;
    }
    //swap은 1부터 시작한다
}


int ku_pgfault_handler(unsigned short virtualADDR){ //16비트 (2byte)
    //접근하려는 vpn이 page table entry의 개수보다 클때
    short offset = (virtualADDR & OFFSET_MASK);
    short VPN = (virtualADDR & VPN_MASK) >> VPN_SHIFT;
    if(VPN >= PFNUM) return 0;

    unsigned short PDindex = (VPN & PD_MASK) >> SHIFT;
    short* PDE = pdbr + PDindex; //pdbr 현재 프로세스의 pdbr로 변경

    
    int PFN = (*PDE & PFN_MASK) >> PFN_SHIFT;
    ptbr = (unsigned short*)(pmem+(PFN << 6));
    unsigned short PTindex = (VPN & PT_MASK);
    short* PTE = ptbr + PTindex;

    //접근하려는 pfn이 disk에 있을 때
    if((*PTE & 0x0001) == 0 && ((*PTE) << 1) != 0){ //present bit가 0인데, pfn이나 dirty가 0이 아니라면 swap out 되어있는 것
        
        int pfn = (*PTE & 0xFFFC) >> 2;
        memset(&swaps[pfn * PAGE_SIZE], 0, PAGE_SIZE); //메모리에서 없애기
        //present bit 1로 바꾸기 (비어있는 메모리에 넣을때 설정할 것이다)
        swaps_free[pfn] = 0; //freelist 0으로 업데이트


         //pfn이 swap offset이므로 swap offset으로 접근한다
         //비어있는 메모리가 있으면 present bit을 1로 설정한다 / pfn을 재설정한다
         //비어있는 메모리가 없으면 (아래와 같음)
    }

    //비어있는 메모리가 있을때
    for(int i = 0; i<PFNUM; i++){
        if(pmem_free[i] == 0){
            *PTE = i << 4; // pfn 저장
            *PTE = *PTE | 0x0001; // present bit 1 설정

            if(swapInList == NULL){
                swapInList = (swapInPageNode *)malloc(sizeof(swapInPageNode));
                swapInList->pid = current->pid;
                swapInList->pteAddress = PTE;
                swapInList->next = NULL;
            }else{
                swapInPageNode* temp = swapInList;
                while (temp->next != NULL) {
                    temp = temp->next;
                }
                temp->next = (swapInPageNode*)malloc(sizeof(swapInPageNode));
                temp->next->pid = current->pid;
                temp->next->pteAddress = PTE;
                temp->next->next = NULL;
            }
            memcpy(&pmem[i * PAGE_SIZE], (char*)1, PAGE_SIZE); //메모리 사용중 암시
            pmem_free[i] = 1;
            //swap in list에 추가한다
            return 0;
        }


    }

    //physical memory가 다 찼을 때
    
    for(int i = 0; i<SFNUM; i += PAGE_SIZE){
        
            if(swaps_free[i] == 0){ //swap space가 남았을 때

                swapInPageNode *swapOutPage = swapInList;
                swapInPageNode *eviction = swapInList; 
                
                if((*(eviction->pteAddress) | 0x0002) == 0){ // dirty bit이 0일때 쫓아내고 추가하기
                    eviction->next = NULL; //첫번째거 쫓아내기 but page directory를 쫓아내면 안된다(how?)
                }
                else{ //dirty bit이 1일땐 swap space에 저장하고 추가하기
                    *PTE = i << 2;
                    *PTE = *PTE | 0x0000; // present bit 1 설정
                    memcpy(&swaps[i * PAGE_SIZE], (char*)1, PAGE_SIZE); //메모리에 해당 current pcb 적재
                    swaps_free[i] = 1;
                    eviction->pteAddress = PTE;
                    eviction->next = NULL;
                } 


                int pfn = *(eviction->pteAddress)&0xFFF0; //pfn을 얻어온다
                *PTE = *(eviction->pteAddress) | 0x0001; // present bit 1 설정

                memcpy(&pmem[pfn * PAGE_SIZE], (char*)1, PAGE_SIZE); //메모리 사용중 암시
                pmem_free[pfn] = 1;

                //swap in list에 추가한다
                swapInPageNode* temp = eviction;
                while (temp->next != NULL) {
                    temp = temp->next;
                }
                temp->next = (swapInPageNode*)malloc(sizeof(swapInPageNode));
                temp->next->pid = current->pid;
                temp->next->pteAddress = PTE;
                temp->next->next = NULL;

                swapInList = eviction;
                return 0;
        //dirty bit이 0이면 물리메모리에서 그냥 버린다
        //swap in list안에 있는 것 중 첫번째를 swap out(swaps)로 옮긴다 (FIFO)
        //옮겨진 swaps의 주소를 저장한다 (pfn)
        //pde, pte를 업데이트한다 (swap offset으로 변경 / present bit 1)
        }
    }

}

int ku_scheduler(unsigned short pid){ //현재 실행되고 있는 current process의 id가 넘어온다
    //다음번 실행할 프로세스를 선정한다
    unsigned short nextpid;
    struct pcbNode *cur = processList;

    if(processList == NULL){
        return 1;
    }else{

        while(1){ //while로 pid를 하나씩 늘려가면서 확인하는 방법 | processlist를 정렬해서 관리하는 방법, 우선 전자를 택했다
            nextpid = (pid+1) % 10; //10이 들어올 경우 다시 0을 실행해야하기 때문

            while(cur != NULL){
                if(cur->pcblock.pid == pid){ 
                    cur = cur->next; //다음 프로세스를 선택
                    *current = cur->pcblock;
                    pdbr = current->pgdir;
                    return 0;
                }
                cur = cur->next;
            }
        }
    }

}

int ku_proc_exit(unsigned short pid){ //종료시킬 pid가 넘어온다
    //process의 pcb를 삭제한다
    //process의 page frame과 swap frame의 자원을 회수한다
    //free list를 업데이트한다

    struct pcbNode* prev;
    struct pcbNode *cur = processList;

    while(cur != NULL){ //for(int i =0; i<pidCount; i++)
        if(cur->pcblock.pid == pid){ //pid가 같은 경우가 있는지 확인한다
           
           fclose(cur->pcblock.fd); //해당 pcb의 file close
           free(cur->pcblock.pgdir); //page directory 자원해제
           free(cur); //해당 pcb 해제
           cur = NULL;


            //리스트에서 해당 프로세스 노드 제거
            if (prev != NULL) {
                prev->next = cur->next;
            } else {
                processList = cur->next;
            }
            free(current); //해당 pcb 해제

            memset(&pmem[pidCount * PAGE_SIZE], 0, PAGE_SIZE);
            swaps_free[pidCount] = 0;

            // pidCount 감소
            pidCount--;

           return 0;
        }
        prev = cur;
        current = cur->next;
    }

    //invalid pid
    return 1;

}

void ku_proc_init(int argc, char *argv[]){ 
    //실행할 수 있는 프로세스들의 pcb, page directory를 생성한다
    //내부적으로 page directory를 할당을 하고 다 0으로 채워넣는다 (추후에 pde, pte로 채워넣어질 것이다)
    //page directory는 swap out되지 않는다

    FILE* fd = fopen("input.txt", "r+");
    int pid, pidSame = 0;
    char processName[100];

    if (fd == NULL) {
        perror("파일 열기 오류");
        return;
    }

    while(1){ //더이상 읽히지 않을때까지
        pidSame = 0;

        if(fscanf(fd, "%d %s", &pid, processName) == EOF){
            printf("파일에서 데이터를 읽을 수 없습니다.\n");
            break;
        }

        printf("%d %s\n", pid, processName);

        if(pidSame == 0){ //pid가 같은게 없는 경우(pid가 같은게 여러개일 경우가 있을까?

            if(processList == NULL){ // process가 하나도 실행되지 않았을 때
                processList = (pcbNode *)malloc(sizeof(pcbNode));
                processList->next = NULL;

                processList->pcblock.pid = pid;
                processList->pcblock.pgdir = (unsigned short*)malloc(sizeof(unsigned short)*32); //page table 생성
                processList->pcblock.fd = fopen(processName, "r+");

                char d;
                int base, bound;
                if (fscanf(processList->pcblock.fd, "%c %d %d", &d, &base, &bound) == EOF) {
                    return;
                }

                printf("%c %d %d\n",d,base,bound);
                processList->pcblock.base = base;
                processList->pcblock.bound = bound;              
                pidCount++;

                for(int i=0; i<PAGE_SIZE; i++){
                    processList->pcblock.pgdir[i] = 0;
                }

                current = NULL;
                current = (struct pcb*)malloc(sizeof(struct pcb));
                current->fd = processList->pcblock.fd;
                current->pgdir = processList->pcblock.pgdir;
                current->pid = processList->pcblock.pid;

                //printf("two : %d %d\n", current->fd, processList->pcblock.fd);

            }else{ // process가 하나 이상 실행됐을 때
                struct pcbNode* temp = processList;
                printf("하나이상\n");
                while(1){

                    if(temp->pcblock.pid == pid){ //pid가 같은 경우가 있는지 확인한다
                        fclose(temp->pcblock.fd);
                        temp->pcblock.fd = fopen(processName, "r+");

                        processList = temp;
                        pidSame = 1;
                        break;
                    }
                    if(temp->next == NULL){
                        printf("하나이상1-2\n");
                        temp->next = (pcbNode*)malloc(sizeof(pcbNode));
                        printf("하나이상2\n");
                        temp = temp->next;
                        temp->next = NULL;
                        temp->pcblock.pid = pid;
                        temp->pcblock.pgdir = (unsigned short*)malloc(sizeof(unsigned short)*32);
                        for(int i=0; i<PAGE_SIZE; i++){
                            temp->pcblock.pgdir[i] = 0;
                        }
                        printf("하나이상3\n");
                        
                        temp->pcblock.fd = fopen(processName, "r+");

                        char d;
                        int base, bound;
                        if (fscanf(temp->pcblock.fd, "%c %d %d", &d, &base, &bound) == EOF) {
                            return;
                        }
                        printf("%c %d %d\n",d,base,bound);
                        temp->pcblock.base = base;
                        temp->pcblock.bound = bound;

                        processList = temp;
                        pidCount++;
                        printf("%d\n", temp->pcblock.fd);

                        break;

                    }
                    temp = temp->next;
                }

            }


            printf("three : %d\n", processList->pcblock.fd); //왜 newprocessnode로 접근하면 오류가 생기지?
            // swap에 pgdir만큼의 크기를 할당해야한다
            memcpy(&pmem[pidCount * PAGE_SIZE], processList->pcblock.pgdir, PAGE_SIZE);
            swaps_free[pidCount] = 1; //pidcount를 곱하는게 맞아?
        }
        printf("\n");
    }
    // 파일 닫기

   //fclose(fd);
    printf("%d %d\n", current->fd, current->pid); //current fd가 됐다가, 안됐다가 한다
    pdbr = current->pgdir;
    

}