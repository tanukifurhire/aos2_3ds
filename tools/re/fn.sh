#!/bin/sh
# fn.sh <hexaddr>... : print decompiled function(s) from dec/all.c
cd "$(dirname "$0")"
for a in "$@"; do
  a=$(printf '%08x' 0x${a#0x})
  awk -v h="@ $a" 'index($0,"// =====")==1{f=(index($0,h)>0)} f' dec/all.c
done
