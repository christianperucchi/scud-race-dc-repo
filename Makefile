# SCUD RACE - Dreamcast tech demo. Requires KallistiOS (source environ.sh) and romdisk/ built by scripts/build_assets.sh
TARGET = scudrace.elf
OBJS = src/game.o romdisk.o
KOS_ROMDISK_DIR = romdisk
KOS_CFLAGS += -O3 -ffast-math -Wall
all: rm-elf $(TARGET)
include $(KOS_BASE)/Makefile.rules
clean: rm-elf
	-rm -f $(OBJS) romdisk.img
rm-elf:
	-rm -f $(TARGET)
$(TARGET): $(OBJS)
	kos-cc -o $(TARGET) $(OBJS)
cdi: $(TARGET)
	scripts/make_cdi.sh
