#define ADDR_SIZE 16 //address size
#define PAGE_SIZE 64
#define PFNUM 6 //4096 // 2^12
#define SFNUM 16384 //2^14
#define VPN_SHIFT 6
#define PFN_SHIFT 4
#define SHIFT 5
#define VPN_MASK 0xFFC0
#define OFFSET_MASK 0x003F
#define PD_MASK 0x03E0
#define PT_MASK 0x001F
#define PFN_MASK 0xFFF0 //0xFFC0?

#include<string.h>

struct pcb *current; // 현재 실행하고 있는 pcb를 가리키는 포인터
unsigned short *pdbr; //page directory base주소를 가리키는 포인터
unsigned short *ptbr; // page table base 주소를 가리키는 포인터
char *pmem, *swaps; //각각 물리 메모리와 swap공간에 대한 base주소를 가리키는 포인터
int pfnum, sfnum; //각각 page frame의 개수
char* pmem_free, *swaps_free; //물리 메모리의 free list
int pidCount = 0; // pid개수
int first = 0;

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
    unsigned short* teAddress; //해당 페이지 정보가 담겨있는 te 주소
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

void swapIn(unsigned short* TE, int distinct){

    if(swapInList == NULL){
        swapInList = (swapInPageNode *)malloc(sizeof(swapInPageNode));
        if(distinct == -1){ //-1이면 PDE
            swapInList->pid = distinct;
        }else{
            swapInList->pid = current->pid;
        }

        swapInList->teAddress = TE;
        swapInList->next = NULL;
    }else{
        swapInPageNode* temp = swapInList;
        while (temp->next != NULL) {
            temp = temp->next;
        }
        temp->next = (swapInPageNode*)malloc(sizeof(swapInPageNode));
         if(distinct == -1){ //-1이면 PDE
             temp->next->pid = distinct;
        }else{
            temp->next->pid = current->pid;
        }

        temp->next->pid = current->pid;
        temp->next->teAddress = TE;
        temp->next->next = NULL;
    }
    printf("swapin에 추가했음\n");
}

void swapOut(swapInPageNode* eviction, swapInPageNode* swapOutPage, unsigned short* PTE, int i, int flag){
    if((*(eviction->teAddress)| 0x0002) == 0){ // dirty bit이 0일때 쫓아내고 추가하기
        eviction = eviction->next; //첫번째거 쫓아내기 but page directory를 쫓아내면 안된다 -> swap in list에 directory 정보를 넣지 않았다
    }
    else{ //dirty bit이 1일땐 swap space에 저장하고 추가하기
        unsigned short *pte;
        *pte = i << 2;
        *pte = *pte | 0x0000; // present bit 0 설정

        swaps[i] = *pte; // swap에 저장한다
        swaps_free[i] = 1;
        eviction = eviction->next; // list의 시작을 다음 노드로 변경
        printf("swap에 저장했음\n");
    } 
    swapInList = eviction; // FIFO에 의해 첫번째 노드를 버리고 다음 노드를 가리킨다

    int pfn = *(swapOutPage->teAddress) & 0xFFF0; //pfn을 얻어온다
    *PTE = *(swapOutPage->teAddress) | 0x0001; // present bit 1 설정
    pmem[i] = *PTE;
    pmem_free[pfn] = 1;
    swapIn(PTE, flag); //swap in list에 추가한다

}


int ku_pgfault_handler(unsigned short virtualADDR){ //16비트 (2byte)

    printf("\npage fault 진입\n");

    short offset = (virtualADDR & OFFSET_MASK);
    short VPN = (virtualADDR & VPN_MASK) >> VPN_SHIFT;

    // 근데 그 전에 bound를 넘는 번지를 접근했으면? -> 접근했다면 page fault가 발생하지 않는다
    if(current->base > virtualADDR || current->bound <= virtualADDR) return 1;

    unsigned short PDindex = (VPN & PD_MASK) >> SHIFT;
    unsigned short* PDE = pdbr + PDindex;

    int PFN = (*PDE & PFN_MASK) >> PFN_SHIFT;
    ptbr = (unsigned short*)(pmem + (PFN << 6));
    unsigned short PTindex = (VPN & PT_MASK);
    unsigned short* PTE = ptbr + PTindex;
    //unsigned short* page_table_entry = ptbr+VPN;
    
    /*disk에 있는 pte 또는 page를 밖으로 뺀다 (pmem이 남아있으면 넣을 것이고, 아니면 eviction에서 넣을 것이기 때문)*/

    if(!(*PDE & 0x1)){ //PDE present bit이 1이면 pte가 pmem에 존재한다 (PDE는 swap out되지 않는다)
        if(*PDE != 0){
            //해당 pte가 disk에 존재하는 것이다
            int pfn = (*PDE & 0xFFFC) >> 2; // swap space offset을 구한다
            swaps[pfn] = 0; // swaps에서 해당 pte 또는 page를 뺀다
            swaps_free[pfn] = 0; // free list 업데이트
        }

        if(!(*PTE & 0x1) && *PTE != 0){  //PTE의 present bit이 0인데, PTE의 값이 0이 아니면(처음 접근하면 무조건 0으로 세팅되어있기 때문)
             //해당 page가 disk에 존재하는 것이다
            int pfn = (*PTE & 0xFFFC) >> 2; // swap space offset을 구한다
            swaps[pfn] = 0;
            swaps_free[pfn] = 0; // free list 업데이트
        }
    }


    /* 물리 메모리가 남아있을 때 */

    for(int i = 0; i < PFNUM; i++){
        if(pmem_free[i] == 0){
            printf("pmem free : %d\n", i);
            if(!(*PDE & 0x1)){
                *PDE = (i << 4) | 0x0001;
                
                pmem[i] = *PDE;
                pmem_free[i] = 1;
                swapIn(PDE, -1);
                
                PFN = (*PDE & PFN_MASK) >> PFN_SHIFT;
                ptbr = (unsigned short*)(pmem + (PFN << 6));
                PTindex = (VPN & PT_MASK);
                PTE = ptbr + PTindex;
                i++;
            }
            *PTE = (i << 4) | 0x0001;
            pmem[i] = *PTE;
            pmem_free[i] = 1;
            swapIn(PTE, 0);

            printf("page fault 성공\n");
            return 0;
        }
    }
    
    /* 물리 메모리가 다 찼을 때 */
    printf("물리 메모리가 다 찼을 때\n");
    for(int i = 0; i<SFNUM; i ++){
        
            if(swaps_free[i] == 0){ //swap space가 남았을 때

                swapInPageNode *swapOutPage = swapInList;
                swapInPageNode *eviction = swapInList; 
                
                if(eviction->pid == -1){ //PTE에 연결된 page가 모두 disk에 있거나, 유효하지 않을 경우 eviction을 해도 된다
                    int flag = 1;
                    unsigned short* pde = eviction->teAddress;
                    int pfn = (*pde & PFN_MASK) >> PFN_SHIFT;
                    ptbr = (unsigned short*)(pmem + (pfn << 6));

                    for(int i = 0 ; i< PAGE_SIZE/2; i++){ //32
                        unsigned short* pte = ptbr + i;
                        if(*pte & 0x1){
                            flag = 0;
                            break;
                        }
                    }

                    if(flag == 1){
                        printf("pte를 뺄 것이다\n");
                        swapOut(eviction, swapOutPage, PTE, i, -1);
                    }
                }
                printf("page를 뺄 것이다\n");
                swapOut(eviction, swapOutPage, PTE, i, 0);

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
    struct pcbNode *cur;

    if(processList == NULL){
        return 1;
    }else{
        //printf("pid : %d pidCount : %d\n", pid, pidCount);
        if(pid == 10 || pid == pidCount-1){ //pid는 0부터 시작하기 때문
            nextpid = first;
        }else{
            nextpid = pid+1;
        }
       
        //printf("\nscheduler 다음번 실행할 pid : %d\n", nextpid);
        cur = processList;
        while(1){  //while로 pid를 하나씩 늘려가면서 확인하는 방법 | processlist를 정렬해서 관리하는 방법, 우선 전자를 택했다
            while(cur != NULL){
                if(cur->pcblock.pid == nextpid){ 
                    current = &cur->pcblock;
                    pdbr = current->pgdir;
                    //printf("scheduler pdbr : %d", pdbr);
                    return 0;
                }
                cur = cur->next;
            }
            nextpid++;
        }
    }

}

int ku_proc_exit(unsigned short pid){ //종료시킬 pid가 넘어온다
    //process의 pcb를 삭제한다
    //process의 page frame과 swap frame의 자원을 회수한다
    //free list를 업데이트한다
    printf("header exit에 들어왔다\n");
    struct pcbNode *prev = NULL; // 이전 노드를 가리킬 포인터
    struct pcbNode *cur = processList; // 현재 검사 중인 노드

    while (cur != NULL) {
        if (cur->pcblock.pid == pid) { // 찾는 pid와 일치하는 경우
            if (cur->pcblock.fd != NULL) {
                fclose(cur->pcblock.fd); // 파일 닫기
            }
            if (cur->pcblock.pgdir != NULL) {
                free(cur->pcblock.pgdir); // 페이지 디렉토리 자원 해제
            }
            
            // 리스트에서 노드 제거
            if (prev != NULL) {
                prev->next = cur->next; // 이전 노드의 next를 현재 노드의 next로 연결
            } else {

                processList = cur->next; // 리스트의 시작이 현재 노드였다면 시작 노드를 다음 노드로 변경
                if(processList != NULL){
                    first = processList->pcblock.pid;
                }         
            }

            free(cur); // 현재 노드 메모리 해제
            cur = NULL; // 포인터 초기화

            // 여기서 메모리와 스왑 공간을 정리하는 로직은 실제 시스템의 자원 관리 방식에 따라 달라질 수 있음
            //memset(&pmem[pidCount * PAGE_SIZE], 0, PAGE_SIZE); // pmem 관련 로직은 시스템의 메모리 관리 상황에 따라 조정 필요
            
            //pmem_free[pidCount] = 0; // 스왑 프레임 해제 !! (이걸 수정해야해)

            // pidCount 감소는 전역 카운터를 조작하므로 주의해서 사용
            // if(pidCount != 0){
            //     pidCount--;
            // }

            return 0; // 성공적으로 처리
        }
        prev = cur; // 다음 노드 검사를 위해 현재 노드를 이전 노드로 설정
        cur = cur->next; // 다음 노드로 이동
    }

    // PID가 유효하지 않거나 찾지 못했을 때
    return 1; // 실패 반환
}


void ku_proc_init(int argc, char *argv[]){ 
    //실행할 수 있는 프로세스들의 pcb, page directory를 생성한다
    //내부적으로 page directory를 할당을 하고 다 0으로 채워넣는다 (추후에 pde, pte로 채워넣어질 것이다)
    //page directory는 swap out되지 않는다

    FILE* fd = fopen("input.txt", "r+");
    int pid;
    char processName[100];

    if (fd == NULL) {
        perror("파일 열기 오류");
        return;
    }

    while(fscanf(fd, "%d %s", &pid, processName) != EOF){ //더이상 읽히지 않을때까지

        //printf("%d %s\n", pid, processName);  //pid가 같은게 없는 경우(pid가 같은게 여러개일 경우가 있을까?

        pcbNode *temp = processList;
        pcbNode *prev = NULL;
        int found = 0;

        // 노드 검색
        while (temp) {
            if (temp->pcblock.pid == pid) {
                fclose(temp->pcblock.fd); // 기존 파일 핸들 닫기
                temp->pcblock.fd = fopen(processName, "r+");
                found = 1;
                break;
            }
            prev = temp;
            temp = temp->next;
        }

        // 새 노드 추가
        if (!found && pidCount < 10) {
            pcbNode *newNode = (pcbNode *)malloc(sizeof(pcbNode));
            if (newNode == NULL) {
                perror("메모리 할당 실패");
                continue;
            }
            newNode->next = NULL;
            newNode->pcblock.pid = pid;
            newNode->pcblock.pgdir = (unsigned short *)calloc(32, sizeof(unsigned short)); //pgdir 초기화가 필요한가?
            newNode->pcblock.fd = fopen(processName, "r+");

            char c;
            if (fscanf(newNode->pcblock.fd, "%c %d %d", &c, &newNode->pcblock.base, &newNode->pcblock.bound) == EOF) {
                //freePcb(&newNode->pcblock);
                free(newNode);
                return;
            }
            //printf("%d %d %d\n", newNode->pcblock.pid, newNode->pcblock.base, newNode->pcblock.bound);

            if (prev) {
                prev->next = newNode;
            } else {
                processList = newNode; // 리스트가 비어있었다면 헤드 설정
            }

            //printf("pidCount %d\n", pidCount);
            pmem[pidCount] = *newNode->pcblock.pgdir;
            pmem_free[pidCount] = 1;
            pidCount++;
            //printf("pidcount\n", pidCount);
            //printf("node : %d\n", newNode->pcblock.fd); //왜 newprocessnode로 접근하면 오류가 생기지?
        }

        // swap에 pgdir만큼의 크기를 할당해야한다
        
        //printf("\n");

    }

    // 파일 닫기
    fclose(fd);
    current = (struct pcb*)malloc(sizeof(struct pcb));
}