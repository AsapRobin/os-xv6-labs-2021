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


#define NBUFMAP_BUCKET 13//根据指导书，13为质数，可以帮助减少哈希冲突
//将 (dev, blockno) 的组合映射到一个 0 到 12（即 13 个桶）的范围内的整数值，即哈希表中对应的桶编号
//根据设备号和区块号快速查找缓存块
#define BUFMAP_HASH(dev, blockno) ((((dev)<<27)|(blockno))%NBUFMAP_BUCKET)

struct {
  struct buf buf[NBUF];
  struct buf buf_hashmap[NBUFMAP_BUCKET];//哈希表
  struct spinlock buf_hashmap_locks[NBUFMAP_BUCKET];//保护哈希表中每个桶的访问
  struct spinlock eject_block_lock;//用于在缓存驱逐和重分配的过程中保护关键区段
} bcache;

void
binit(void)
{
  // 初始化 buf_hashmap
  for(int i=0;i<NBUFMAP_BUCKET;i++) {
    // 为每个桶（哈希表中的每个元素）初始化自旋锁
    initlock(&bcache.buf_hashmap_locks[i], "bcache_bufmap");
    // 将每个桶的 `next` 指针初始化为 0（空指针）
    bcache.buf_hashmap[i].next = 0;
  }

  // 初始化缓存块
  for(int i=0;i<NBUF;i++){
    struct buf *b = &bcache.buf[i];
    // 为每个缓存块初始化睡眠锁
    initsleeplock(&b->lock, "buffer");
    // 将缓存块的最后使用时间设置为 0
    b->lastuse = 0;
    // 将缓存块的引用计数设置为 0
    b->refcnt = 0;
    // 将所有缓存块链入到 buf_hashmap[0] 的链表中
    b->next = bcache.buf_hashmap[0].next;
    bcache.buf_hashmap[0].next = b;
  }

  // 初始化驱逐操作的锁
  initlock(&bcache.eject_block_lock , "bcache_eviction");
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno)
{
  struct buf *b;

  // 根据设备号和块号计算哈希值，找到对应的桶
  uint key = BUFMAP_HASH(dev, blockno);
  // 获取该桶的锁
  acquire(&bcache.buf_hashmap_locks[key]);

  // 遍历桶中的缓存块链表，检查目标块是否已缓存
  for(b = bcache.buf_hashmap[key].next; b; b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      // 增加引用计数
      b->refcnt++;
      // 释放桶的锁
      release(&bcache.buf_hashmap_locks[key]);
      // 获取缓存块的睡眠锁
      acquiresleep(&b->lock);
      return b;
    }
  }

  // 释放当前桶的锁，避免死锁
  release(&bcache.buf_hashmap_locks[key]);
  // 获取驱逐锁，确保替换过程的唯一性
  acquire(&bcache.eject_block_lock );

  // 再次检查目标块是否已缓存
  for(b = bcache.buf_hashmap[key].next; b; b = b->next){
     if(b->dev == dev && b->blockno == blockno){
      // 增加引用计数
      acquire(&bcache.buf_hashmap_locks[key]);
      b->refcnt++;
      release(&bcache.buf_hashmap_locks[key]);
      // 释放驱逐锁
      release(&bcache.eject_block_lock );
      // 获取缓存块的睡眠锁
      acquiresleep(&b->lock);
      return b;
    }
  }
  
  //遍历所有桶，寻找引用计数为 0 且最近最少使用的缓存块，以便替换。
  //如果找到适合替换的缓存块，将其从原来的桶移除并重新哈希到目标桶中。
  struct buf *before_least = 0; 
  uint holding_bucket = -1;
  for(int i = 0; i < NBUFMAP_BUCKET; i++){

    acquire(&bcache.buf_hashmap_locks[i]);
    int newfound = 0; 
    for(b = &bcache.buf_hashmap[i]; b->next; b = b->next) {
      if(b->next->refcnt == 0 && (!before_least || b->next->lastuse < before_least->next->lastuse)) {
        before_least = b;
        newfound = 1;
      }
    }
    if(!newfound) {
      release(&bcache.buf_hashmap_locks[i]);
    } else {
      if(holding_bucket != -1) release(&bcache.buf_hashmap_locks[holding_bucket]);
      holding_bucket = i;
    }
  }
  if(!before_least) {
    panic("bget: no buffers");
  }
  b = before_least->next;
  
  if(holding_bucket != key) {
    
    before_least->next = b->next;
    release(&bcache.buf_hashmap_locks[holding_bucket]);
    acquire(&bcache.buf_hashmap_locks[key]);
    b->next = bcache.buf_hashmap[key].next;
    bcache.buf_hashmap[key].next = b;
  }
  
 // 更新缓存块信息
 b->dev = dev;
 b->blockno = blockno;
 b->refcnt = 1;
 b->valid = 0;
 // 释放锁
 release(&bcache.buf_hashmap_locks[key]);
 release(&bcache.eject_block_lock );
 // 获取缓存块的睡眠锁并返回
 acquiresleep(&b->lock);
 return b;

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

  uint key = BUFMAP_HASH(b->dev, b->blockno);

  acquire(&bcache.buf_hashmap_locks[key]);
  b->refcnt--;
  if (b->refcnt == 0) {
    b->lastuse = ticks;
  }
  release(&bcache.buf_hashmap_locks[key]);
}

void
bpin(struct buf *b) {
  uint key = BUFMAP_HASH(b->dev, b->blockno);

  acquire(&bcache.buf_hashmap_locks[key]);
  b->refcnt++;
  release(&bcache.buf_hashmap_locks[key]);
}

void
bunpin(struct buf *b) {
  uint key = BUFMAP_HASH(b->dev, b->blockno);

  acquire(&bcache.buf_hashmap_locks[key]);
  b->refcnt--;
  release(&bcache.buf_hashmap_locks[key]);
}