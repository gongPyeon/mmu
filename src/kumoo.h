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
char* pmem_free; //물리 메모리의 free list

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
    pfnum = PFNUM;
    sfnum = SFNUM;

    pmem_free = (char*)malloc(sizeof(char)*PFNUM);
    for(int i=0; i<PFNUM; i++){
        pmem_free[i] = 0;
    }

    for(int i = 0; i<PFNUM*64; i++){
        pmem[i] = 0;
    }
    //phy는 0부터 시작한다
    for(int i = 0; i<SFNUM*64; i++){
        swaps[i] = 0;
    }
    //swap은 1부터 시작한다

    //free list를 초기화한다
}


int ku_pgfault_handler(unsigned short virtualADDR){ //16비트
    //접근하려는 vpn이 page table entry의 개수보다 클때
    unsigned short offset = (virtualADDR & OFFSET_MASK);
    unsigned short VPN = (virtualADDR & VPN_MASK) >> VPN_SHIFT;
    if(VPN >= PFNUM) return 0;

    unsigned short PDindex = (VPN & PD_MASK) >> SHIFT;
    unsigned short PDE = pdbr + PDindex; //pdbr 현재 프로세스의 pdbr로 변경
    //current->pgdir = PDE;

    
    ptbr = (PDE & PFN_MASK) >> PFN_SHIFT;
    unsigned short PTindex = (VPN & PT_MASK);
    unsigned char* PTE = ptbr + PTindex;
    //current->pgt = PTE;

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

// unsigned short PDindex = (VPN & PD_MASK) >> SHIFT;
//     unsigned short PDEAddr = pdbr + (PDindex * sizeof(ADDR_SIZE));
        
//     unsigned short PTE = (PDEAddr & PFN_MASK) >> PFN_SHIFT;
//     unsigned short PTindex = (VPN & PT_MASK);
//     unsigned PTEAddr = PTE + (PTindex * sizeof(ADDR_SIZE));

//     unsigned short PFN = (PTEAddr & PFN_MASK) >> PFN_SHIFT;
    // unsigned short PFN = (PTE & PFN_MASK) >> PFN_SHIFT;
    // char *page_table_entry = ptbr + ;
    

int ku_scheduler(unsigned short){

}

int ku_proc_exit(unsigned short){

}

void ku_proc_init(int argv1, char* argv2){

}