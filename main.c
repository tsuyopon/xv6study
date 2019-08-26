/*
 * XV6ブートシーケンスについて
 *  1. 自前ブートローダーの場合
 *      bootasm.Sが16bitモードから起動する場合の処理で、32bitへの移行とgdtの初期設定をしてからbootmain.cのbootmain()を呼ぶ
 *      bootmain()からメモリにロードされたカーネルのバイナリがELFかチェック
 *      ELFだった場合はセグメントをロードしていく
 *
 *  2. Grunの場合
 *      xv6はgrubのブートプロトコルに対応しているのでgrubから起動した場合は最初にentry.Sのコードに制御が移り、
 *      この段階ですでにプロテクトモードになっているはず。
 *
 *  参考: http://kernhack.hatenablog.com/entry/2012/08/28/230701
 */

/*

   仮想アドレス空間を左に、物理アドレス空間を右に示す
   参考: http://yshigeru.blogspot.jp/2011/12/xv6.html

        4G->+----+
           /|    |\
     Device |    |
           \|    |
0xFE000000->|----|
           /|    |
            |    |
Free Memory |    | Kernel Space
            |    |
           \|    |
       end->|    |        +----+<-4G
            |    |        |    |\
 +0x100000->|    |        |    | Device
  KERNBASE->|    |        |    |/
           /|----|/       |----|<-PHYSTOP
            |    |        |    |\
            |    |        |    |
            |    |        |    |
            |    |        |    | Extended Memory
            |    |        |    |
 User Space |    |        |    |
            |    |        |    |
            |    |        |----|/
            |    |        |    |<-0x100000
            |    |        |    |<-I/O Space
            |    |        |    |<-640k
           \|    |        |    |<-Base Memory
         0->+----+        +----+
            Vertual      Physical*

*/
#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "x86.h"

static void startothers(void);
static void mpmain(void)  __attribute__((noreturn));
extern pde_t *kpgdir;
extern char end[]; // first address after kernel loaded from ELF file

// Bootstrap processor starts running C code here.
// Allocate a real stack and switch to it, first
// doing some setup required for memory allocator to work.
int
main(void)
{
  // kinit1では、end〜4MBまでの物理アドレス空間を4KBのページに分割し、フリーリストに登録する。
  kinit1(end, P2V(4*1024*1024)); // phys page allocator

  // この関数はentry.Sで設定したページディレクトリ（entrypgdir）からkpgdirにページディレクトリを設定しなおす
  kvmalloc();      // kernel page table

  // マルチプロセッサの場合にAPを初期化する。ただし、AP(Another Processor?: non-boot processor)の起動はここではやらないでstartothers()で実施している
  mpinit();        // collect info about this machine

  // local APICの初期化を行う。
  // APICはAdvanced Programmable Interrupt Controllerの略で、インテルにより開発された、x86アーキテクチャにおける割り込みコントローラを指す。
  // APICはCPU毎に存在し、CPUに内蔵されているLocal APICとICH(SouthBridge)に内蔵されているI/O APICから構成されている
  lapicinit();

  // セグメントの設定を行う
  seginit();       // set up segments

  // CPU数の出力
  cprintf("\ncpu%d: starting xv6\n\n", cpu->id);

  picinit();       // interrupt controller
  ioapicinit();    // another interrupt controller

  // コンソールに関する初期化処理の設定
  consoleinit();   // I/O devices & their interrupts
  
  // UART(8250 Universal Asynchronous Receiver/Transmitterの略称) => http://ja.wikipedia.org/wiki/UART
  // シリアルポートの初期化を行っている
  uartinit();      // serial port

  // プロセスロックの初期化
  pinit();         // process table

  // 割り込みベクタの設定
  tvinit();        // trap vectors

  // bcacheのロック初期化 + buf構造体を利用したNBUF個のバッファ(固定長配列)を初期化する。バッファキャッシュへのアクセスはbcache.head経由で行われる
  binit();         // buffer cache

  // ftableのロック初期化
  fileinit();      // file table

  // icacheのロック初期化
  iinit();         // inode cache

  // 
  ideinit();       // disk

  if(!ismp)
    timerinit();   // uniprocessor timer

  // 起動時に利用したプロセッサ以外の他のプロセッサを開始する(最初は1つのプロセッサしか起動しないので...)
  startothers();   // start other processors

  // 4MBからPHYSTOPまでの物理アドレス空間を、フリーリストに登録する。
  kinit2(P2V(4*1024*1024), P2V(PHYSTOP)); // must come after startothers()

  userinit();      // first user process

  // Finish setting up this processor in mpmain.
  mpmain();
}

// Other CPUs jump here from entryother.S.
static void
mpenter(void)
{
  switchkvm(); 
  seginit();
  lapicinit();
  mpmain();
}

// Common CPU setup code.
static void
mpmain(void)
{
  cprintf("cpu%d: starting\n", cpu->id);
  idtinit();       // load idt register
  xchg(&cpu->started, 1); // tell startothers() we're up
  scheduler();     // start running processes
}

pde_t entrypgdir[];  // For entry.S

// Start the non-boot (AP) processors. main()の中でのみ呼ばれる
static void
startothers(void)
{
  extern uchar _binary_entryother_start[], _binary_entryother_size[];
  uchar *code;
  struct cpu *c;
  char *stack;

  // Write entry code to unused memory at 0x7000.
  // The linker has placed the image of entryother.S in
  // _binary_entryother_start.
  code = p2v(0x7000);
  memmove(code, _binary_entryother_start, (uint)_binary_entryother_size);

  for(c = cpus; c < cpus+ncpu; c++){
    if(c == cpus+cpunum())  // We've started already.
      continue;

    // Tell entryother.S what stack to use, where to enter, and what 
    // pgdir to use. We cannot use kpgdir yet, because the AP processor
    // is running in low  memory, so we use entrypgdir for the APs too.
    stack = kalloc();
    *(void**)(code-4) = stack + KSTACKSIZE;
    *(void**)(code-8) = mpenter;
    *(int**)(code-12) = (void *) v2p(entrypgdir);

    lapicstartap(c->id, v2p(code));

    // wait for cpu to finish mpmain()
    while(c->started == 0)
      ;
  }
}

// Boot page table used in entry.S and entryother.S.
// Page directories (and page tables), must start on a page boundary,
// hence the "__aligned__" attribute.  
// Use PTE_PS in page directory entry to enable 4Mbyte pages.
__attribute__((__aligned__(PGSIZE)))
pde_t entrypgdir[NPDENTRIES] = {
  // Map VA's [0, 4MB) to PA's [0, 4MB)
  [0] = (0) | PTE_P | PTE_W | PTE_PS,
  // Map VA's [KERNBASE, KERNBASE+4MB) to PA's [0, 4MB)
  [KERNBASE>>PDXSHIFT] = (0) | PTE_P | PTE_W | PTE_PS,
};

//PAGEBREAK!
// Blank page.
//PAGEBREAK!
// Blank page.
//PAGEBREAK!
// Blank page.
