// Physical memory allocator, intended to allocate
// memory for user processes, kernel stacks, page table pages,
// and pipe buffers. Allocates 4096-byte pages.

#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "spinlock.h"

void freerange(void *vstart, void *vend);
extern char end[]; // first address after kernel loaded from ELF file

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  int use_lock;
  struct run *freelist;
} kmem;

// 参考: http://yshigeru.blogspot.jp/2011/12/xv6.html
//
// Initialization happens in two phases.
// 1. main() calls kinit1() while still using entrypgdir to place just
// the pages mapped by entrypgdir on free list.
// 2. main() calls kinit2() with the rest of the physical pages
// after installing a full page table that maps them on all cores.
void
kinit1(void *vstart, void *vend)
{
  // kmemのロック初期化
  initlock(&kmem.lock, "kmem");
  kmem.use_lock = 0;
  freerange(vstart, vend);
}

void
kinit2(void *vstart, void *vend)
{
  freerange(vstart, vend);
  kmem.use_lock = 1;
}

// end〜4MBまでの物理アドレス空間を4KBのページに分割し、フリーリストに登録する。この処理を行うのが、freerange関数とkfree関数である。
void
freerange(void *vstart, void *vend)
{
  // 引数vstartとvendで渡された仮想アドレス空間（範囲）についてPGSIZE(4KB)ごとにkfree関数を呼び出す
  char *p;
  p = (char*)PGROUNDUP((uint)vstart);
  for(; p + PGSIZE <= (char*)vend; p += PGSIZE)
    kfree(p);
}

//PAGEBREAK: 21
// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(char *v)
{
  struct run *r;

  // ページ境界に合ってない 又は endよりも小さい 又は v2p(v)がPHYSTOP以上である
  if((uint)v % PGSIZE || v < end || v2p(v) >= PHYSTOP)
    panic("kfree");

  // vから1ページ分、メモリ領域を1で埋める
  // Fill with junk to catch dangling refs.
  memset(v, 1, PGSIZE);

  // ロック開始
  if(kmem.use_lock)
    acquire(&kmem.lock);

  // vをフリーリストにつなぐ
  r = (struct run*)v;
  r->next = kmem.freelist;
  kmem.freelist = r;

  // ロック終了
  if(kmem.use_lock)
    release(&kmem.lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
char*
kalloc(void)
{
  struct run *r;

  if(kmem.use_lock)
    acquire(&kmem.lock);
  r = kmem.freelist;
  if(r)
    kmem.freelist = r->next;
  if(kmem.use_lock)
    release(&kmem.lock);
  return (char*)r;
}

