#!/bin/bash
# record.sh START END MAXWALL  -> seg_START.mkv + tele_START.log
S=$1; E=$2; W=${3:-260}
cd "$(dirname "$0")/.."
source /opt/toolchains/dc/kos/environ.sh >/dev/null
kos-cc $KOS_CFLAGS -O3 -ffast-math -DVIDEO -DSTART_FRAME=$S -DEND_FRAME=$E -c src/game.c -o build/vgame.o 2>/dev/null && kos-cc -o build/video.elf build/vgame.o romdisk.o || exit 1
export DISPLAY=:97 LIBGL_ALWAYS_SOFTWARE=1 SDL_AUDIODRIVER=dummy HOME=${FLYCAST_HOME:-$HOME}
Xvfb :97 -screen 0 800x600x24 </dev/null >/dev/null 2>&1 & XP=$!; sleep 1; xsetroot -cursor /tmp/blank.xbm /tmp/blank.xbm
${FLYCAST:-flycast} build/video.elf </dev/null >tele_$S.log 2>&1 & FP=$!
ffmpeg -loglevel error -y -f x11grab -framerate 20 -video_size 640x480 -i :97.0+80,60 -c:v mjpeg -q:v 2 seg_$S.mkv </dev/null & GP=$!
xsetroot -cursor /tmp/blank.xbm /tmp/blank.xbm; st=$(date +%s)
while [ $(( $(date +%s) - st )) -lt $W ]; do grep -aq '^END' tele_$S.log && break; sleep 2; done
sleep 1; kill -INT $GP; sleep 2; kill $FP 2>/dev/null; sleep 1; kill -9 $FP 2>/dev/null; kill $XP 2>/dev/null; wait 2>/dev/null
echo "wall $(( $(date +%s) - st ))s  telemetry $(grep -ac '^T ' tele_$S.log) lines  last: $(grep -a '^T ' tele_$S.log | tail -1 | cut -d' ' -f2-3)"
