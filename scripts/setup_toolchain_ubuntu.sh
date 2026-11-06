#!/usr/bin/env bash
# Toolchain used to develop this project (Ubuntu 24.04): distro SH compiler + newlib 4.6 with
# the KallistiOS patch + KallistiOS. The official route (kos-chain / DreamSDK) works too.
set -euo pipefail
sudo apt-get install -y gcc-sh-elf binutils-sh-elf libnewlib-sh-elf-dev genisoimage \
     libjpeg-dev libpng-dev libelf-dev curl patch make python3-numpy python3-pil gcc-multilib
TC=/opt/toolchains/dc; sudo mkdir -p $TC && sudo chown "$USER" $TC; cd $TC
[ -d kos ] || { curl -sL https://codeload.github.com/KallistiOS/KallistiOS/tar.gz/refs/heads/master | tar xz; mv KallistiOS-master kos; }
# compiler prefix; Ubuntu's sh-elf-gcc defaults to big-endian SH1, KOS needs -ml -m4-single
mkdir -p sh-elf/bin; for t in /usr/bin/sh-elf-*; do ln -sf $t sh-elf/bin/; done
rm sh-elf/bin/sh-elf-gcc; printf '#!/bin/sh\nexec /usr/bin/sh-elf-gcc -ml -m4-single "$@"\n' > sh-elf/bin/sh-elf-gcc; chmod +x sh-elf/bin/sh-elf-gcc
# newlib 4.6 + KOS patch, only the ml/m4-single multilib
mkdir -p build && cd build
[ -d newlib-src ] || { curl -sL https://codeload.github.com/cygwin/cygwin/tar.gz/refs/tags/newlib-4.6.0 | tar xz; mv cygwin-newlib-4.6.0 newlib-src;
  (cd newlib-src && patch -p1 < $TC/kos/utils/kos-chain/patches/targets/newlib-4.6.0.20260123-kos.diff); }
mkdir -p nl && cd nl
../newlib-src/configure --target=sh-elf --prefix=$TC/build/nl-inst --disable-multilib --disable-newlib-supplied-syscalls \
  --enable-newlib-io-c99-formats --disable-libgloss --disable-werror CFLAGS_FOR_TARGET="-ml -m4-single -O2"
make -j"$(nproc)" all-target-newlib && make install-target-newlib
I=$TC/build/nl-inst/sh-elf; N=/usr/sh-elf/include; L=/usr/sh-elf/lib/ml/m4-single
sudo cp -a $N /usr/sh-elf/include.bak-3.3; sudo rm -rf $N; sudo cp -a $I/include $N; sudo cp $I/lib/lib*.a $L/
K=$TC/kos/include; sudo cp $K/pthread.h $N; sudo cp $K/sys/_pthreadtypes.h $K/sys/dirent.h $N/sys; sudo cp $K/machine/time.h $N/machine; sudo ln -nsf $K/kos $N
# KallistiOS (the prebuilt AICA driver is used, no ARM compiler needed)
cd $TC/kos && cp doc/environ.sh.sample environ.sh
sed -i 's|^export KOS_BASE=.*|export KOS_BASE="/opt/toolchains/dc/kos"|; s|^export DC_ARM_BASE=.*|export DC_ARM_BASE="/nonexistent"|' environ.sh
bash -c 'source environ.sh && make'
echo "Done. Run: source /opt/toolchains/dc/kos/environ.sh"
