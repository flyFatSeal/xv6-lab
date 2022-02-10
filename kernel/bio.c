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


#define NULL ((void *)0)

struct hash {
  struct spinlock lock;
  struct buf *start;
};

struct 
{
  struct spinlock lock;
  struct buf buf[NBUF];

  struct buf *head;
  struct hash table[NBUCKET];
} bcache;

void
binit(void)
{
  initlock(&bcache.lock, "bcache");

  // init all bucket lock
  for (int i = 0; i < NBUCKET; i++){
    initlock(&bcache.table[i].lock, "bucket");
  }
  // init c-clock list of buffers
  for (int j = 1; j <= NBUF; j++)
  {
    bcache.buf[j-1].next = &bcache.buf[j];
    if(j == NBUF){
      bcache.buf[j-1].next = &bcache.buf[0];
    }else{
      bcache.buf[j-1].next = &bcache.buf[j];
    }
    initsleeplock(&bcache.buf[j-1].lock, "buffer");
    bcache.buf[j-1].bucket = -1;
    bcache.buf[j-1].bufno = j-1;
  }
  bcache.head = &bcache.buf[0];
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno)
{
  struct buf *b;
  int index = GETHASH(blockno);
  acquire(&bcache.table[index].lock);
  struct buf *start = bcache.table[index].start;
  struct buf *cur = start;
  struct buf *pre = NULL;
  // Is the block already cached?
  while(cur != NULL){
    b = cur;
    if (b->blockno == blockno && b->dev == dev)
    {
      b->refcnt++;
      // move linklist
      if(pre != NULL){
        pre->down = pre->down->down; 
      }
      if(cur != start){
        cur->down = start;
        bcache.table[index].start = cur;
      }
      release(&bcache.table[index].lock);
      acquiresleep(&b->lock);
      return b;
    }
    pre = cur;
    cur = cur->down;
  }
  release(&bcache.table[index].lock);

  // Not cached.
  // Recycle the least recently used (LRU) unused buffer.
  acquire(&bcache.lock);

  acquire(&bcache.table[index].lock);

  start = bcache.table[index].start;
  cur = start;
  pre = NULL;
  // recheck is the block already cached?
  while(cur != NULL){
    b = cur;
    if (b->blockno == blockno && b->dev == dev)
    {
      b->refcnt++;
      // move linklist
      if(pre != NULL){
        pre->down = pre->down->down; 
      }
      if(cur != start){
        cur->down = start;
        bcache.table[index].start = cur;
      }
      release(&bcache.table[index].lock);
      release(&bcache.lock);
      acquiresleep(&b->lock);
      return b;
    }
    pre = cur;
    cur = cur->down;
  }


  for (b = bcache.head->next; b != bcache.head; b = b->next)
  {
    int bindex = b->bucket;
    if (bindex != index && bindex > - 1)
      acquire(&bcache.table[bindex].lock);
    if(b->refcnt == 0 ){
      // steal from other hash list
      if(bindex > -1){
        struct buf *bstart = bcache.table[bindex].start;
        struct buf *bcur = bstart;
        struct buf *bpre = NULL;
        while(bcur != NULL ){
          if(bcur->bufno == b->bufno){
            if(bpre != NULL){
              bpre->down = bpre->down->down; 
            }
            if(bstart == bcur){
              bcache.table[bindex].start = bcur->down;
            }
            break;
          }
          bpre = bcur;
          bcur = bcur->down;
        }
      }

      b->dev = dev;
      b->blockno = blockno;
      b->valid = 0;
      b->refcnt = 1;
      b->bucket = index; 
      
      // c-clock head point
      bcache.head = b;
      // modify cur bucket start pointer
      if(b != start)
        b->down = start;
      bcache.table[index].start = b;

      if (bindex != index && bindex > - 1)
        release(&bcache.table[bindex].lock);
      release(&bcache.table[index].lock);
      release(&bcache.lock);
      acquiresleep(&b->lock);
      return b;
    }
    if (bindex != index && bindex > - 1)
      release(&bcache.table[bindex].lock);
  }

  release(&bcache.table[index].lock);

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

  acquire(&bcache.table[b->bucket].lock);
  b->refcnt--;
  release(&bcache.table[b->bucket].lock);
}

void
bpin(struct buf *b) {
  acquire(&bcache.table[b->bucket].lock);
  b->refcnt++;
  release(&bcache.table[b->bucket].lock);
}

void
bunpin(struct buf *b) {
  acquire(&bcache.table[b->bucket].lock);
  b->refcnt--;
  release(&bcache.table[b->bucket].lock);
}


