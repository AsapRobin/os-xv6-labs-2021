// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

void freerange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run {
  struct run *next;
};

struct kmem{
  struct spinlock lock;
  struct run *freelist;
} ;

struct kmem kmemList[NCPU];//为每个CPU都细化一个kmem类


void
kinit()
{
  for(int i = 0; i < NCPU; i++){
    char name[9] = {0};
    snprintf(name, 8, "kmem_%d", i); //每个锁的名字不一样
    initlock(&(kmemList[i].lock),name);
  }
  freerange(end, (void*)PHYSTOP);
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE)
    kfree(p);
}

// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;


  //关中断，获取当前CPU的id
  push_off();
  int i = cpuid();
  pop_off();
  
  acquire(&kmemList[i].lock);
  r->next = kmemList[i].freelist;
  kmemList[i].freelist = r;
  release(&kmemList[i].lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;
  // 关闭中断并获取当前的 CPU ID
  push_off();
  int i = cpuid();  // 获取当前 CPU ID
  // 获取当前 CPU 的 freelist 锁
  acquire(&kmemList[i].lock);
  r = kmemList[i].freelist;
  // 如果当前 CPU 有可用的空闲页，则直接使用
  if (r) {
    kmemList[i].freelist = r->next;
  } else {
    // 如果当前 CPU 的 freelist 为空，则从其他 CPU 窃取
    for (int j = 0; j < NCPU; j++) {
      if (j == i) continue;  // 跳过当前 CPU

      acquire(&kmemList[j].lock);  // 获取其他 CPU 的 freelist 锁
      if (kmemList[j].freelist) {
        r =kmemList[j].freelist;
        kmemList[j].freelist = r->next;  // 窃取成功
        release(&kmemList[j].lock);  // 释放锁
        break;
      }
      release(&kmemList[j].lock);  // 没有找到空闲页，释放锁
    }
  }
  // 释放当前 CPU 的 freelist 锁
  release(&kmemList[i].lock);
  // 打开中断
  pop_off();

  if (r)
    memset((char*)r, 5, PGSIZE); // fill with junk

  return (void*)r;
}
