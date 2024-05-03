#include <stdio.h>
#include <stdlib.h>
#include "kumoo.h"

#define SCHED	0
#define PGFAULT	1
#define EXIT	2
#define TSLICE	5

struct handlers{
       int (*sched)(unsigned short);
       int (*pgfault)(unsigned short);
       int (*exit)(unsigned short);
}kuos;

void ku_dump_pmem(void){
    for(int i = 0; i < (64 << 12); i++){
        printf("%x ", pmem[i]);
    }
    printf("\n");
}
void ku_dump_swap(void){
    for(int i = 0; i < (64 << 14); i++){
        printf("%x ", swaps[i]);
    }
    printf("\n");
}

void ku_reg_handler(int flag, int (*func)(unsigned short)){ //핸들러 등록
	switch(flag){
		case SCHED:
			kuos.sched = func;
			break;
		case PGFAULT:
			kuos.pgfault = func;
			break;
		case EXIT:
			kuos.exit = func;
			break;
		default:
			exit(0);
	}
}

int ku_traverse(char va){
	int pd_index, pt_index, pa;
    unsigned short *ptbr;
	short *pte, *pde;
    int PFN;

	pd_index = (va & 0xFFC0) >> 11; //va에서 page directory index를 계산한다
	pde = pdbr + pd_index; //pdbr + page directory index로 page directory entry를 계산한다

	if(!*pde)
		return -1;
    
    PFN = (*pde & 0xFFF0) >> 4; //page directory entry에서 pfn을 얻는다
    ptbr = (unsigned short*)(pmem + (PFN << 6)); //물리주소 + pfn(<<6은 페이지 크기를 곱한 것이다)으로 page table base register을 계산한다

    pt_index = (va & 0x07C0) >> 6; //va에서 page table index를 계산한다
    pte = ptbr + pt_index; //page table base register + page table index로 page table entry를 접근한다

    if(!*pte)
        return -1;

    PFN = (*pte & 0xFFF0) >> 4; //page frame number를 찾는다

    pa = (PFN << 6)+(va & 0x3F); //물리주소는 [page frame number / va의 offset]


	return pa; //물리주소를 반환한다
}


void ku_os_init(void){
    /* Initialize physical memory*/
    pmem = (char*)malloc(64 << 12); //256kb 할당
    swaps = (char*)malloc(64 << 14); //1mb 할당
    /* Init free list*/
    ku_freelist_init();
    /*Register handler*/
	ku_reg_handler(SCHED, ku_scheduler);
	ku_reg_handler(PGFAULT, ku_pgfault_handler);
	ku_reg_handler(EXIT, ku_proc_exit);
}

void op_read(){
    unsigned char va;
    int addr, pa, ret = 0;
    char sorf = 'S';
    /* get Address from the line */
    if(fscanf(current->fd, "%d", &addr) == EOF){
        /* Invaild file format */
        return;
    }
    va = addr & 0xFFFF; //가상 주소 16진수로 변환
    pa = ku_traverse(va); //해당 가상주소에 대해 유효한 물리주소를 반환한다

    if (pa < 0){
        /* page fault!*/
        ret = kuos.pgfault(va);
    } 
    if (ret > 0){ //page fault가 실패하면
        /* No free page frames or SEGFAULT */
        sorf = 'E';
        ret = kuos.exit(current->pid);
        if (ret > 0){
            /* invalid PID */
            return;
        }
    }
    else { //page fault가 성공하면
        pa = ku_traverse(va);
        sorf = 'F';
    }

    if (pa < 0){
        printf("%d: %d -> (%c)\n", current->pid, va, sorf);
    }
    else {
        printf("%d: %d -> %d (%c)\n", current->pid, va, pa, sorf);
    }
  
}

void op_write(){
    unsigned char va;
    int addr, pa, ret = 0;
    char input ,sorf = 'S';
    /* get Address from the line */
    if(fscanf(current->fd, "%d %c", &addr, &input) == EOF){
        /* Invaild file format */
        return;
    }
    va = addr & 0xFFFF;
    pa = ku_traverse(va);

    if (pa < 0){
        /* page fault!*/
        ret = kuos.pgfault(va);
    } 
    if (ret > 0){
        /* No free page frames or SEGFAULT */
        sorf = 'E';
        ret = kuos.exit(current->pid);
        if (ret > 0){
            /* invalid PID */
            return;
        }
    }
    else {
        pa = ku_traverse(va);
        sorf = 'F';
    }

    if (pa < 0){
        printf("%d: %d -> (%c)\n", current->pid, va, sorf);
    }
    else {
        *(pmem + pa) = input;
        printf("%d: %d -> %d (%c)\n", current->pid, va, pa, sorf);
    }

}

void do_ops(char op){
    char sorf;
    int ret;
    switch(op){
        case 'r':
            op_read();
        break;

        case 'w':
            op_write();
        break;

        case 'e':
            ret = kuos.exit(current->pid);
            if (ret > 0){
                /* invalid PID */
                return;
            }
        break;
    }

}

void ku_run_procs(void){
	unsigned char va;
    char sorf;
	int addr, pa, i;
    char op;
    int ret;

	do{
		if(!current) //현재 실행되고 있는 프로세스가 없으면
			exit(0); //종료한다

		for( i=0 ; i<TSLICE ; i++){
            /* Get operation from the line */
			if(fscanf(current->fd, "%c", &op) == EOF){ //현재 파일 디스크립터의 연산정보(w,r,e)를 넣는다
                /* Invaild file format */
                return;
			}
            do_ops(op); //연산 함수를 실행한다
		}

		ret = kuos.sched(current->pid);
        /* No processes */
        if (ret > 0)
            return;

	}while(1);
}

int main(int argc, char *argv[]){
	/* System initialization */
	ku_os_init();
	/* Per-process initialization */
	ku_proc_init(atoi(argv[1]), argv[2]);
	/* Process execution */
	ku_run_procs();

	return 0;
}