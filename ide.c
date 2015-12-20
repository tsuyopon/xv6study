// Simple PIO-based (non-DMA) IDE driver code.

#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "x86.h"
#include "traps.h"
#include "spinlock.h"
#include "buf.h"

#define IDE_BSY       0x80
#define IDE_DRDY      0x40
#define IDE_DF        0x20
#define IDE_ERR       0x01

#define IDE_CMD_READ  0x20
#define IDE_CMD_WRITE 0x30

// idequeue points to the buf now being read/written to the disk.
// idequeue->qnext points to the next buf to be processed.
// You must hold idelock while manipulating queue.

static struct spinlock idelock;
static struct buf *idequeue;

static int havedisk1;
static void idestart(struct buf*);


// idewait関数は、ビジービット（IDE_BSY）がクリアされ準備完了ビット（IDE_DRDY）がセットされるまで、その状態ビットをポーリングする。
// Wait for IDE disk to become ready.
static int
idewait(int checkerr)
{
  int r;

  while(((r = inb(0x1f7)) & (IDE_BSY|IDE_DRDY)) != IDE_DRDY) 
    ;
  if(checkerr && (r & (IDE_DF|IDE_ERR)) != 0)
    return -1;
  return 0;
}

// picenable関数とioapicenable関数を呼び、IDE_IRQ割り込みを有効にする。
void
ideinit(void)
{
  int i;

   // ideロックを初期化する
  initlock(&idelock, "ide");

  // ユニプロセッサ上で割り込みを有効にする
  picenable(IRQ_IDE);

  // マルチプロセッサ上で割り込みを有効にするが、最後のCPU（ncpu-1）についてのみである
  // つまりプロセッサが2つあるシステムでは、CPU 1がディスク割り込みを制御する。
  ioapicenable(IRQ_IDE, ncpu - 1);

  // ディスクがコマンドを受け付けれる状態になるまで待つ。
  idewait(0);
  
  // ディスク1を選択するためにI/Oポート0x1f6へ書き込み、そしてディスクの状態ビットが準備完了状態を表すようになるまで待つ。
  // Check if disk 1 is present
  outb(0x1f6, 0xe0 | (1<<4));
  for(i=0; i<1000; i++){

    // PCのマザーボードは、I/Oポート0x1f7でディスクハードウェアの状態ビットを提供する。
    if(inb(0x1f7) != 0){
      havedisk1 = 1;
      break;
    }
  }
  
  // Switch back to disk 0.
  outb(0x1f6, 0xe0 | (0<<4));
}

/*
 * アセンブラでディスクから読み出す場合の命令を発行する場合
 *   1. Wait for the disk to be ready (CPU reads location 0x1F7).
 *   2. Store # of sectors you want to read into location 0x1F2.
 *   3. Store sector offset into locations 0x1F3 - 0x1F6. The sector offset is disk location, given by a 32-bit quantity. This allows for referencing up to 2^32 * 2^9 = 2^41 bytes.
 *   4. Store READ command into location 0x1F7.
 *   5. Wait for disk to be ready.
 *   6. Get results as a sector into CPU.
 *   7. Store results into RAM.
 *
 * 参考: http://cs.ucla.edu/classes/spring08/cs111/scribe/2/index.html
 */
// IDEディスクに対してバッファの内容を読み書きする
// Start the request for b.  Caller must hold idelock.
static void
idestart(struct buf *b)
{
  if(b == 0)
    panic("idestart");

  idewait(0);
  outb(0x3f6, 0);  // generate interrupt
  outb(0x1f2, 1);  // number of sectors
  outb(0x1f3, b->sector & 0xff);
  outb(0x1f4, (b->sector >> 8) & 0xff);
  outb(0x1f5, (b->sector >> 16) & 0xff);
  outb(0x1f6, 0xe0 | ((b->dev&1)<<4) | ((b->sector>>24)&0x0f));

  /*
   * バッファにB_DIRTYフラグがセットされていたら，ディスクへの書込み
   * 要求であると判断し，対応するセクタにバッファの内容を書き込む．
   * そうでない場合には，読込み要求であると判断し，指定したセクタか
   * らデータを読み込む．
   */
  if(b->flags & B_DIRTY){
    outb(0x1f7, IDE_CMD_WRITE);
    outsl(0x1f0, b->data, 512/4);
  } else {
    outb(0x1f7, IDE_CMD_READ);
  }
}

// Interrupt handler.
void
ideintr(void)
{
  struct buf *b;

  // First queued buffer is the active request.
  acquire(&idelock);
  if((b = idequeue) == 0){
    release(&idelock);
    // cprintf("spurious IDE interrupt\n");
    return;
  }
  idequeue = b->qnext;

  // Read data if needed.
  if(!(b->flags & B_DIRTY) && idewait(1) >= 0)
    insl(0x1f0, b->data, 512/4);
  
  // Wake process waiting for this buf.
  b->flags |= B_VALID;
  b->flags &= ~B_DIRTY;
  wakeup(b);
  
  // Start disk on next buf in queue.
  if(idequeue != 0)
    idestart(idequeue);

  release(&idelock);
}

//PAGEBREAK!
// Sync buf with disk. 
// If B_DIRTY is set, write buf to disk, clear B_DIRTY, set B_VALID.
// Else if B_VALID is not set, read buf from disk, set B_VALID.
void
iderw(struct buf *b)
{
  struct buf **pp;

  if(!(b->flags & B_BUSY))
    panic("iderw: buf not busy");
  if((b->flags & (B_VALID|B_DIRTY)) == B_VALID)
    panic("iderw: nothing to do");
  if(b->dev != 0 && !havedisk1)
    panic("iderw: ide disk 1 not present");

  acquire(&idelock);  //DOC:acquire-lock

  // Append b to idequeue.
  b->qnext = 0;
  for(pp=&idequeue; *pp; pp=&(*pp)->qnext)  //DOC:insert-queue
    ;
  *pp = b;
  
  // Start disk if necessary.
  if(idequeue == b)
    idestart(b);
  
  // Wait for request to finish.
  while((b->flags & (B_VALID|B_DIRTY)) != B_VALID){
    sleep(b, &idelock);
  }

  release(&idelock);
}
