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

// $B;29M(B: http://yshigeru.blogspot.jp/2011/12/xv6.html
//
// Initialization happens in two phases.
// 1. main() calls kinit1() while still using entrypgdir to place just
// the pages mapped by entrypgdir on free list.
// 2. main() calls kinit2() with the rest of the physical pages
// after installing a full page table that maps them on all cores.
void
kinit1(void *vstart, void *vend)
{
  // kmem$B$N%m%C%/=i4|2=(B
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

// end$B!A(B4MB$B$^$G$NJ*M}%"%I%l%96u4V$r(B4KB$B$N%Z!<%8$KJ,3d$7!"%U%j!<%j%9%H$KEPO?$9$k!#$3$N=hM}$r9T$&$N$,!"(Bfreerange$B4X?t$H(Bkfree$B4X?t$G$"$k!#(B
void
freerange(void *vstart, void *vend)
{
  // $B0z?t(Bvstart$B$H(Bvend$B$GEO$5$l$?2>A[%"%I%l%96u4V!JHO0O!K$K$D$$$F(BPGSIZE(4KB)$B$4$H$K(Bkfree$B4X?t$r8F$S=P$9(B
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

  // $B%Z!<%86-3&$K9g$C$F$J$$(B $BKt$O(B end$B$h$j$b>.$5$$(B $BKt$O(B v2p(v)$B$,(BPHYSTOP$B0J>e$G$"$k(B
  if((uint)v % PGSIZE || v < end || v2p(v) >= PHYSTOP)
    panic("kfree");

  // v$B$+$i(B1$B%Z!<%8J,!"%a%b%jNN0h$r(B1$B$GKd$a$k(B
  // Fill with junk to catch dangling refs.
  memset(v, 1, PGSIZE);

  // $B%m%C%/3+;O(B
  if(kmem.use_lock)
    acquire(&kmem.lock);

  // v$B$r%U%j!<%j%9%H$K$D$J$0(B
  r = (struct run*)v;
  r->next = kmem.freelist;
  kmem.freelist = r;

  // $B%m%C%/=*N;(B
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

