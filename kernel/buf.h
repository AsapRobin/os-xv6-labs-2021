struct buf {
  int valid;   // has data been read from disk?
  int disk;    // does disk "own" buf?
  uint dev;
  uint blockno;
  struct sleeplock lock;
  uint refcnt;
  uchar data[BSIZE];

  uint prev_use_time; // 记录上一次使用的时间，时间戳
  struct buf * next; // 记录下一个节点
};