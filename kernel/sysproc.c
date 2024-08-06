#include "types.h"
#include "riscv.h"
#include "param.h"
#include "defs.h"
#include "date.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"

uint64
sys_exit(void)
{
  int n;
  if(argint(0, &n) < 0)
    return -1;
  exit(n);
  return 0;  // not reached
}

uint64
sys_getpid(void)
{
  return myproc()->pid;
}

uint64
sys_fork(void)
{
  return fork();
}

uint64
sys_wait(void)
{
  uint64 p;
  if(argaddr(0, &p) < 0)
    return -1;
  return wait(p);
}

uint64
sys_sbrk(void)
{
  int addr;
  int n;

  if(argint(0, &n) < 0)
    return -1;
  
  addr = myproc()->sz;
  if(growproc(n) < 0)
    return -1;
  return addr;
}

uint64
sys_sleep(void)
{
  int n;
  uint ticks0;


  if(argint(0, &n) < 0)
    return -1;
  acquire(&tickslock);
  ticks0 = ticks;
  while(ticks - ticks0 < n){
    if(myproc()->killed){
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}


#ifdef LAB_PGTBL

extern pte_t * walk(pagetable_t, uint64, int);
int
sys_pgaccess(void)
{
  // lab pgtbl: your code here.
  uint64 mask=0;//表示每个页面是否被访问过
  uint64 virtual_addr;//用户传入的起始虚拟地址
  uint64 mask_va;//用户传入的位掩码在用户空间的地址
  int sum_pages;//用户要检查的页面数量

                    
  if(argaddr(0, &virtual_addr) < 0)
    return -1;
  if(argint(1, &sum_pages) < 0)
     return -1;
  if(sum_pages > MAXSCAN)
     return -1;  
  if(argaddr(2, &mask_va) < 0)
    return -1;

  pte_t* pte;
  
  for(int i=0;i<sum_pages;i++){
    //walk 函数用于在页表中找到与给定虚拟地址对应的页表项
   if((pte = walk(myproc()->pagetable, virtual_addr, 0)) == 0)
    panic("pgaccess : walk failed");
   if(*pte & PTE_A){
    //如果页表项值的第六位是1，则表示该页面被访问过，应将mask上的对应位设置为1，同时清空该页面的PET_A
    mask |= 1 << i;	
    *pte &= ~PTE_A;		// 将PTE_A清空
  }
  virtual_addr+=PGSIZE;//移动到下一个页面
  }

 // 将结果拷贝到用户空间
  if(copyout(myproc()->pagetable,mask_va , (char*)&mask, sizeof(mask)) < 0)
    return -1;

 return 0;

}
#endif

uint64
sys_kill(void)
{
  int pid;

  if(argint(0, &pid) < 0)
    return -1;
  return kill(pid);
}

// return how many clock tick interrupts have occurred
// since start.
uint64
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}
