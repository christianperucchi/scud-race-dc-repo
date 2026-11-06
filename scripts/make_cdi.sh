#!/usr/bin/env bash
# Builds a self-boot CDI from scudrace.elf. Needs KOS utils (scramble, makeip), genisoimage and cdi4dc.
set -euo pipefail
cd "$(dirname "$0")/.."
U=${KOS_BASE:?source environ.sh first}/utils
mkdir -p build/disc/cd
sh-elf-objcopy -R .stack -O binary scudrace.elf build/disc/scudrace.bin
$U/scramble/scramble build/disc/scudrace.bin build/disc/cd/1ST_READ.BIN
$U/makeip/makeip -f -g "SCUD RACE DC TECH DEMO" -c "HOMEBREW" -d 19981127 -e V0.200 build/disc/IP.BIN
genisoimage -C 0,11702 -V SCUDRACE -G build/disc/IP.BIN -r -J -l -input-charset iso8859-1 -quiet -o build/disc/scudrace.iso build/disc/cd
cdi4dc build/disc/scudrace.iso scudrace.cdi
