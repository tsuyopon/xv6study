#!/usr/bin/perl

# このファイルはMakefile中で実行されている

open(SIG, $ARGV[0]) || die "open $ARGV[0]: $!";

$n = sysread(SIG, $buf, 1000);

if($n > 510){
  print STDERR "boot block too large: $n bytes (max 510)\n";
  exit 1;
}

print STDERR "boot block is $n bytes (max 510)\n";

# 512バイトの最後のバイトがaa55だと起動ディスクとして認識されるとのこと。以下で挿入してる
# (参考) http://msyksphinz.hatenablog.com/entry/2015/09/15/020000
$buf .= "\0" x (510-$n);
$buf .= "\x55\xAA";

open(SIG, ">$ARGV[0]") || die "open >$ARGV[0]: $!";
print SIG $buf;
close SIG;
