# Host build of the IIGS core: make -C components/apple2gs/host
#   build/gs_host [frames] [out.ppm]   (see host_main.c)
# The 65C816 is esp32emu's; the machine's devices are Clemens's (clemens/)
GS        := ..
CLEM      := ../clemens
W65816    := ../../../../esp32emu/components/w65816
EMUDBG    := ../../../../esp32emu/components/emudbg
BUILD     := build
CFLAGS    := -O2 -g -Wall -Wno-unused-parameter -Wno-unused-variable -Wno-unused-function \
             -Wno-unused-but-set-variable -Wno-sign-compare \
             -I$(GS)/include -I$(GS) -I$(CLEM) -I$(W65816)/include -I$(EMUDBG)/include

GS_SRCS   := $(wildcard $(GS)/*.c)
CLEM_SRCS := $(wildcard $(CLEM)/*.c)
OBJS      := $(patsubst $(GS)/%.c,$(BUILD)/gs/%.o,$(GS_SRCS)) \
             $(patsubst $(CLEM)/%.c,$(BUILD)/clem/%.o,$(CLEM_SRCS)) \
             $(BUILD)/w65816.o $(BUILD)/w65816_dis.o $(BUILD)/w65816_dbg.o \
             $(BUILD)/emudbg.o $(BUILD)/emudbg_posix.o

all: $(BUILD)/gs_host

$(BUILD)/gs_host: host_main.c $(OBJS)
	$(CC) $(CFLAGS) $^ -lm -lpthread -o $@

$(BUILD)/gs/%.o: $(GS)/%.c $(wildcard $(GS)/*.h $(GS)/include/*.h)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/clem/%.o: $(CLEM)/%.c $(wildcard $(CLEM)/*.h)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/%.o: $(W65816)/%.c $(W65816)/include/w65816.h $(EMUDBG)/include/emudbg.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/%.o: $(EMUDBG)/%.c $(EMUDBG)/include/emudbg.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf $(BUILD)

.PHONY: all clean
