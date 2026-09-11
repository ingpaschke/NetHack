# GEM host for ATW800/2 transputer NetHack: the win/gem renderer on top
# of the toolchain's ST host library (<T800>/atari: link, boot, iserver
# and t8call servers, wire library).
#
#   make            NH5ATW.PRG  (real ATW link)
#                 + NH5ATWD.PRG (scripted fake transputer, runs on hatari)
#
# T800 is the llvm-t800 install, NH the NetHack root.

T800   ?= /opt/llvm-t800
T8HOST ?= $(T800)/atari
NH     ?= ../../..
CC      = m68k-atari-mint-gcc

CFLAGS  = -c -O2 -m68000 -I$(T8HOST)/include -I.. \
	-I$(NH)/include -I$(NH)/sys/atari -I$(NH)/win/gem \
	-I$(NH)/win/share -I$(NH)/lib/e_gem \
	-DNOTTYGRAPHICS -DNO_TERMS \
	-DCROSSCOMPILE -DCROSSCOMPILE_TARGET -DCROSS_TO_ATARI $(XCFLAGS)
LFLAGS  = -m68000 -Wl,--mprg-flags=0x01
LIBS    = -L$(T8HOST)/lib -lt8host -L$(NH)/lib/e_gem -le_gem -lgem

HOSTHDR = $(wildcard $(T8HOST)/include/*.h) ../nhcall.h
GEMOBJ  = wingem1.o gr_rect.o load_img.o
COMMON  = t8gem.o rpcgem.o $(GEMOBJ)

all: NH5ATW.PRG NH5ATWD.PRG

NH5ATW.PRG: $(COMMON) $(T8HOST)/lib/tlink_supexec.o
	$(CC) $(LFLAGS) -o $@ $^ $(LIBS)

NH5ATWD.PRG: $(COMMON) $(T8HOST)/lib/tlink_script.o
	$(CC) $(LFLAGS) -o $@ $^ $(LIBS)

t8gem.o rpcgem.o: %.o: %.c $(HOSTHDR)
	$(CC) $(CFLAGS) -o $@ $<

$(GEMOBJ): %.o: $(NH)/win/gem/%.c
	$(CC) $(CFLAGS) -o $@ $<

clean:
	rm -f *.o NH5ATW.PRG NH5ATWD.PRG

.PHONY: all clean
