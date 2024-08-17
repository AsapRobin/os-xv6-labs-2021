// Buffer cache.
//
// The buffer cache is a linked list of buf structures holding
// cached copies of disk block contents.  Caching disk blocks
// in memory reduces the number of disk reads and also provides
// a synchronization point for disk blocks used by multiple processes.
//
// Interface:
// * To get a buffer for a particular disk block, call bread.
// * After changing buffer data, call bwrite to write it to disk.
// * When done with the buffer, call brelse.
// * Do not use the buffer after calling brelse.
// * Only one process at a time can use a buffer,
//     so do not keep them longer than necessary.


#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"
#include "buf.h"
  
#define prime     13
#define NBUCKET   prime 
#define GET_KEY(dev, blockno) ((blockno) % prime)

struct {

  struct buf hash_table[NBUCKET]; // 申请prime个哈希表（通过单链表维护），总共的BUF数量为NBUF
  struct spinlock lock_bucket[NBUCKET]; // 为每个桶分配一个锁
  
  struct buf buf[NBUF];

} bcache;

void
binit(void)
{
  // 初始化桶的锁
  char bucket_lock_name[10];
  for(int i = 0; i < NBUCKET; i++) {
    snprintf(bucket_lock_name, 10, "bcache%d", i);
    initlock(&bcache.lock_bucket[i], bucket_lock_name);
    bcache.hash_table[i].next = 0;
  }

  // 把所有的buf放入第一个桶中，类似上一个实验把所有的空闲内存放在第一个cpu中
  struct buf * b;
  for(int i = 0; i < NBUF; i++) {
    b = &bcache.buf[i];

    b->prev_use_time = 0;
    b->refcnt = 0;

    initsleeplock(&b->lock, "buffer");

    b->next = bcache.hash_table[0].next;
    bcache.hash_table[0].next = b;
  }

}


// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno)
{
  struct buf *b;

  // Is the block already cached?
  int key = GET_KEY(dev, blockno); // 找到桶的下标
  acquire(&bcache.lock_bucket[key]); //获取这个桶的锁

  // 遍历key对应的桶查询
  for(b = bcache.hash_table[key].next; b; b = b->next) {
    if(b->dev == dev && b->blockno == blockno) { // 命中
      b->refcnt++;      
      release(&bcache.lock_bucket[key]); 
      acquiresleep(&b->lock);
      return b;
    }
  }

  // 没有命中cache，先从当前的key对应的桶中寻找
  int mn = ticks, key_replace = -1;
  struct buf * b_prev_replace = 0;

  for(b = &bcache.hash_table[key]; b->next; b = b->next) {
    if(b->next->prev_use_time <= mn && b->next->refcnt == 0) {
      b_prev_replace = b;
      mn = b->next->prev_use_time;
    }
  }
  if(b_prev_replace) { // 在这个桶里找到了
    b = b_prev_replace->next;
    b->dev = dev;
    b->blockno = blockno;
    b->valid = 0;
    b->refcnt = 1;
    release(&bcache.lock_bucket[key]);
    acquiresleep(&b->lock);
    return b;
  }
  
  for(int i = 0; i < NBUCKET; i++) {

    if(i == key) continue;

    acquire(&bcache.lock_bucket[i]);
    mn = ticks;
    
    for(b = &bcache.hash_table[i]; b->next; b = b->next) {
      if(b->next->prev_use_time <= mn && b->next->refcnt == 0) {
        mn = b->next->prev_use_time;
        b_prev_replace = b;
        key_replace = i;
      }
      if(b_prev_replace && b_prev_replace->next && key_replace >= 0) 
      { // 对bucket[i]中的buf寻找最近最少使用的buf,然后进行修改，这样其实就避免了环路的锁，但并不是真正意义上的LRU
        b = b_prev_replace->next;
        
        // 从旧的桶中删去
        b_prev_replace->next = b->next;
        // 在新的桶中添加
        b->next = bcache.hash_table[key].next;
        bcache.hash_table[key].next = b;
        
        b->dev = dev;
        b->blockno = blockno;
        b->valid = 0;
        b->refcnt = 1;
        
        release(&bcache.lock_bucket[i]);
        
        release(&bcache.lock_bucket[key]);

        acquiresleep(&b->lock);
        // printf("new buf :%d\n", blockno);
        return b;
      }
    }
    
    release(&bcache.lock_bucket[i]);
    
  }
  printf("no buffers: %d\n", blockno);
  release(&bcache.lock_bucket[key]);
  panic("bget: no buffers");
}




// Return a locked buf with the contents of the indicated block.
struct buf*
bread(uint dev, uint blockno)
{
  struct buf *b;

  b = bget(dev, blockno);
  if(!b->valid) {
    virtio_disk_rw(b, 0);
    b->valid = 1;
  }
  return b;
}

// Write b's contents to disk.  Must be locked.
void
bwrite(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("bwrite");
	virtio_disk_rw(b, 1);
}


// Release a locked buffer.
void
brelse(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("brelse");

  releasesleep(&b->lock);

  int key = GET_KEY(b->dev, b->blockno);
  acquire(&bcache.lock_bucket[key]);

  b->refcnt--;
  if (b->refcnt == 0) {
    // no one is waiting for it.
    // 更新时间戳
    b->prev_use_time = ticks;
  }

  release(&bcache.lock_bucket[key]);
  
}

void
bpin(struct buf *b) {
  
  int key = GET_KEY(b->dev, b->blockno);
  acquire(&bcache.lock_bucket[key]);

  b->refcnt++;

  release(&bcache.lock_bucket[key]);
}

void
bunpin(struct buf *b) {

  int key = GET_KEY(b->dev, b->blockno);
  acquire(&bcache.lock_bucket[key]);

  b->refcnt--;

  release(&bcache.lock_bucket[key]);
}