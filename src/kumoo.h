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

struct pcb *current; // 현재 실행하고 있는 pcb를 가리키는 포인터
unsigned short *pdbr; //page directory base주소를 가리키는 포인터
unsigned short *ptbr; // page table base 주소를 가리키는 포인터
char *pmem, *swaps; //각각 물리 메모리와 swap공간에 대한 base주소를 가리키는 포인터
int pfnum, sfnum; //각각 page frame의 개수
unsigned short* pmem_free, *swaps_free; //물리 메모리의 free list

struct pcb{
    unsigned short pid;
    FILE *fd;
    unsigned short *pgdir; //page directory에 대한 주소
    unsigned short *pgt; //page table에 대한 주소
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

    pmem_free = (unsigned short*)malloc(sizeof(unsigned short)*PFNUM);
    for(int i=0; i<PFNUM; i++){
        pmem_free[i] = 0;
    }

    swaps_free = (unsigned short*)mallock(sizeof(unsigned short)*SFNUM);
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


int ku_pgfault_handler(unsigned short virtualADDR){ //16비트
    //접근하려는 vpn이 page table entry의 개수보다 클때
    unsigned short offset = (virtualADDR & OFFSET_MASK);
    unsigned short VPN = (virtualADDR & VPN_MASK) >> VPN_SHIFT;
    if(VPN >= PFNUM) return 0;

    unsigned short PDindex = (VPN & PD_MASK) >> SHIFT;
    short* PDE = pdbr + PDindex; //pdbr 현재 프로세스의 pdbr로 변경

    
    int PFN = (*PDE & PFN_MASK) >> PFN_SHIFT;
    ptbr = (unsigned short*)(pmem+(PFN << 6));
    unsigned short PTindex = (VPN & PT_MASK);
    unsigned char* PTE = ptbr + PTindex;

    //접근하려는 pfn이 disk에 있을 때
    if((*PTE & 0x0001) == 0 && ((*PTE) << 1) != 0){ //present bit가 0인데, pfn이나 dirty가 0이 아니라면 swap out 되어있는 것
         //pfn이 swap offset이므로 swap offset으로 접근한다
         //비어있는 메모리가 있으면 present bit을 1로 설정한다 / pfn을 재설정한다
         //비어있는 메모리가 없으면 (아래와 같음)
         return 1;
    }

    //비어있는 메모리가 있을때
    for(int i = 0; i<PFNUM; i++){
        if(pmem_free[i] == 0){
            *PTE = i << 4; // pfn 저장
            *PTE = *PTE | 0x0001; // present bit 1 설정
            pmem_free[i] = 1;

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
            //swap in list에 추가한다
            return 1;
        }
    }

    //physical memory가 다 찼을 때
    
    for(int i = 0; i<SFNUM; i += PAGE_SIZE){
        if((*PTE | 0x0002) == 0){ // dirty bit이 0일때
                
        }else{
            if(swaps[i] == 0){
            
        }
        //dirty bit이 0이면 그냥 버린다
        //swap in list안에 있는 것 중 첫번째를 swap out(swaps)로 옮긴다 (FIFO)
        //옮겨진 swaps의 주소를 저장한다 (pfn)
        //pde, pte를 업데이트한다 (swap offset으로 변경 / present bit 1)
        }
    }


}

int ku_scheduler(unsigned short){

}

int ku_proc_exit(unsigned short){

}

void ku_proc_init(int argv1, char* argv2){ 
    //실행할 수 있는 프로세스들의 pcb, page directory를 생성한다
    //내부적으로 page directory를 할당을 하고 다 0으로 채워넣는다 (추후에 pde, pte로 채워넣어질 것이다)
    //page directory는 swap out되지 않는다

    FILE* fd = fopen("input.txt", "r");
    int pid, pidSame = 0;
    char processName[100];

    if (fd == NULL) {
        perror("파일 열기 오류");
        return;
    }

    while(1){ //더이상 읽히지 않을때까지
        if(fscanf(fd, "%d %s", &pid, processName) == EOF){
            printf("파일에서 데이터를 읽을 수 없습니다.\n");
            fclose(fd);
            return; //break역할
        }

        printf("%d %s\n", pid, processName);
        processList = (pcbNode*)malloc(sizeof(pcbNode));

        while(1){
            if(processList->pcblock.pid == pid){ //pid가 같은 경우가 있는지 확인한다
                pidSame = 1;
            }
        }

        if(pidSame == 1){ //pid가 같은 경우 실행파일만 변경한다
            processList->pcblock.fd = fopen(processName, "r");
            *current = processList->pcblock;

        }else{ //pid가 같은게 없는 경우(pid가 같은게 여러개일 경우가 있을까?)
            struct pcb process = processList->pcblock;
            process.pid = pid;
            process.fd = fopen(processName, "r");
            process.pgdir = malloc(PAGE_SIZE); //64 byte할당

            if(processList == NULL){ //process가 하나도 실행되지 않았을때
                processList->pcblock = process;
                processList->next = NULL;
                *current = processList->pcblock;
                
            }else{ //process가 하나 이상 실행됐을 때
                while(processList->next == NULL){
                    processList = processList->next;
                }
                processList->pcblock = process;
                processList->next = NULL;
                *current = processList->pcblock;
            }

            for(int i=0; i<PAGE_SIZE; i++){
                process.pgdir[i] = 0;
            }
        }
    }
   
    // 파일 닫기
    fclose(fd);

}