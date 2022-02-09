struct buf {
  int valid;   // has data been read from disk?
  int disk;    // does disk "own" buf?
  uint dev;
  uint blockno;
  struct sleeplock lock;
  uint refcnt;
  int bucket; // hashbucket index
  int bufno; 
  struct buf *next; // c-clock list
  struct buf *down; // hash bucket list
  uchar data[BSIZE];
};

