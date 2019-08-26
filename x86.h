// Routines to let C code use special x86 instructions.

static inline uchar
inb(ushort port)
{
  uchar data;

  asm volatile("in %1,%0" : "=a" (data) : "d" (port));
  return data;
}

// cld(Clear Direction Flag)はEFLAGSレジスタのDFフラグをセットします。
//   DFフラグがセットされている間は、文字列(バイト操作)を行うとインデックスレジスタ(ESI or EDI, both)がインクリメントされる
// rep命令については
//   (参考) http://www.fermimn.gov.it/linux/quarta/x86/rep.htm
static inline void
insl(int port, void *addr, int cnt)
{
  asm volatile("cld; rep insl" :
               "=D" (addr), "=c" (cnt) :
               "d" (port), "0" (addr), "1" (cnt) :
               "memory", "cc");
}

//in, outはI/Oポートを読み書きする命令です。これができることがアセンブラ最大の利点です
static inline void
outb(ushort port, uchar data)
{
  asm volatile("out %0,%1" : : "a" (data), "d" (port));
}

static inline void
outw(ushort port, ushort data)
{
  asm volatile("out %0,%1" : : "a" (data), "d" (port));
}

static inline void
outsl(int port, const void *addr, int cnt)
{
  asm volatile("cld; rep outsl" :
               "=S" (addr), "=c" (cnt) :
               "d" (port), "0" (addr), "1" (cnt) :
               "cc");
}

static inline void
stosb(void *addr, int data, int cnt)
{
  asm volatile("cld; rep stosb" :
               "=D" (addr), "=c" (cnt) :
               "0" (addr), "1" (cnt), "a" (data) :
               "memory", "cc");
}

static inline void
stosl(void *addr, int data, int cnt)
{
  asm volatile("cld; rep stosl" :
               "=D" (addr), "=c" (cnt) :
               "0" (addr), "1" (cnt), "a" (data) :
               "memory", "cc");
}

struct segdesc;

// GDTR(Global Discriptor Table Register)に論理アドレスから物理アドレスへのマッピングの先頭アドレスを格納する。
// このために使う命令がlgdt命令である。
static inline void
lgdt(struct segdesc *p, int size)
{
  volatile ushort pd[3];

  pd[0] = size-1;
  pd[1] = (uint)p;
  pd[2] = (uint)p >> 16;

  asm volatile("lgdt (%0)" : : "r" (pd));
}

struct gatedesc;

// lidt命令は割り込みベクターを登録するための命令です。
static inline void
lidt(struct gatedesc *p, int size)  // lidt = Load Intterupt Descripter Table
{
  volatile ushort pd[3];

  pd[0] = size-1;
  pd[1] = (uint)p;
  pd[2] = (uint)p >> 16;

  asm volatile("lidt (%0)" : : "r" (pd));
}

// LTR命令はTSSをセットするCPU命令
static inline void
ltr(ushort sel)
{
  asm volatile("ltr %0" : : "r" (sel));
}

static inline uint
readeflags(void)
{
  uint eflags;
  asm volatile("pushfl; popl %0" : "=r" (eflags));
  return eflags;
}

static inline void
loadgs(ushort v)
{
  asm volatile("movw %0, %%gs" : : "r" (v));
}

// EFLAGSのIFフラグを0にする 
// IF(Interrupt enable Flag)フラグは割り込み禁止フラグ。割り込みを禁止したい場合にセットする
static inline void
cli(void)
{
  asm volatile("cli");
}

// EFLAGSのIFフラグを1にする 
// IFフラグは割り込み可能フラグ。割り込みを有効化したい場合にセットする。
// CPUはマスク可能な外部からのハードウェア割り込みに応答するようになる。NMIには影響しない。
static inline void
sti(void)
{
  asm volatile("sti");
}

// "xchg src, dest"の場合にはsrcとdestを交換する
// オペランドの1つがメモリアドレスの場合には、操作はLOCKプリフィックスが指定される(ここではすでに指定されているが...)のでパフォーマンス低下となる
static inline uint
xchg(volatile uint *addr, uint newval)
{
  uint result;
  
  // The + in "+m" denotes a read-modify-write operand.
  asm volatile("lock; xchgl %0, %1" :
               "+m" (*addr), "=a" (result) :
               "1" (newval) :
               "cc");
  return result;
}

// CR2(Control Register2)にはページフォルトを発生させた命令がアクセスしようとしたメモリのリニアアドレスを設定する (rcr2 = register control register2)
// 参考: http://caspar.hazymoon.jp/OpenBSD/annex/intel_arc.html
static inline uint
rcr2(void)
{
  uint val;
  asm volatile("movl %%cr2,%0" : "=r" (val));
  return val;
}

// ページディレクトリのベース制御用レジスタCR3(Control Register3)に値を設定する (lcr3 = load control register3)
// 参考: http://caspar.hazymoon.jp/OpenBSD/annex/intel_arc.html
static inline void
lcr3(uint val) 
{
  asm volatile("movl %0,%%cr3" : : "r" (val));
}

//PAGEBREAK: 36
// Layout of the trap frame built on the stack by the
// hardware and by trapasm.S, and passed to trap().
struct trapframe {
  // registers as pushed by pusha
  uint edi;
  uint esi;
  uint ebp;
  uint oesp;      // useless & ignored
  uint ebx;
  uint edx;
  uint ecx;
  uint eax;

  // rest of trap frame
  ushort gs;
  ushort padding1;
  ushort fs;
  ushort padding2;
  ushort es;
  ushort padding3;
  ushort ds;
  ushort padding4;
  uint trapno;

  // below here defined by x86 hardware
  uint err;
  uint eip;
  ushort cs;
  ushort padding5;
  uint eflags;

  // below here only when crossing rings, such as from user to kernel
  uint esp;
  ushort ss;
  ushort padding6;
};
