// Format of an ELF executable file

#define ELF_MAGIC 0x464C457FU  // "\x7FELF" in little endian

// File header
// 参考: http://softwaretechnique.jp/OS_Development/Tips/ELF/elf01.html
struct elfhdr {
  uint magic;  // must equal ELF_MAGIC
  uchar elf[12];
  ushort type;
  ushort machine;   // オブジェクトファイルに必要なアーキテクチャを指定する。
  uint version;
  uint entry;       // OSがプロセスを実行するときに最初に実行する仮想アドレスが記録される。
  uint phoff;       // オブジェクトファイルに格納されているプログラムヘッダテーブルのオフセットが記録されています。
  uint shoff;
  uint flags;
  ushort ehsize;    // ELFヘッダのサイズが格納されます
  ushort phentsize;
  ushort phnum;     // プログラムヘッダーテーブルのエントリー数が記録される。
  ushort shentsize;
  ushort shnum;
  ushort shstrndx;
};

// Program section header
struct proghdr {
  uint type;
  uint off;
  uint vaddr;
  uint paddr;
  uint filesz;
  uint memsz;
  uint flags;
  uint align;
};

// Values for Proghdr type
#define ELF_PROG_LOAD           1

// Flag bits for Proghdr flags
#define ELF_PROG_FLAG_EXEC      1
#define ELF_PROG_FLAG_WRITE     2
#define ELF_PROG_FLAG_READ      4
