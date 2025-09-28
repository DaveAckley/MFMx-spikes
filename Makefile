TT_METAL_BASEDIR:=/data/ackley/PART4/code/D/tt-metal/tt_metal
TT_METAL_HW_BASEDIR:=$(TT_METAL_BASEDIR)/hw
TT_3RDPARTY_BASEDIR:=$(TT_METAL_BASEDIR)/third_party
API_BASEDIR:=$(TT_3RDPARTY_BASEDIR)/umd/device/api
BUILDRELEASE_BASEDIR:=/data/ackley/PART4/code/D/tt-metal/build_Release
UMD_INCLUDES+=-I$(TT_3RDPARTY_BASEDIR)/umd/src/firmware/riscv
UMD_INCLUDES+=-I$(TT_METAL_HW_BASEDIR)/inc
UMD_INCLUDES+=-I$(BUILDRELEASE_BASEDIR)/include
UMD_INCLUDES+=-I$(API_BASEDIR)
UMD_LIBDIRS+=-L$(BUILDRELEASE_BASEDIR)/lib
#UMD_LIBDIRS+=-L$(BUILDRELEASE_BASEDIR)/tt_metal/third_party/umd
UMD_LIBS+=-ldevice
UMD_DLLPATHS+=-Wl,-R/data/ackley/PART4/code/D/tt-metal/build_Release/lib
#UMD_INCLUDES+=-I$(TT_UMD_BASEDIR)/common
#UMD_INCLUDES+=-I$(TT_UMD_BASEDIR)/device

DEBUGFLAG:=-g
PROGNAME:=$(notdir $(CURDIR))
PROGDIR:=./bin
PROG:=$(PROGDIR)/$(PROGNAME)
BUILDDIR:=./build
SRC:=$(wildcard src/*.cpp src/*.c)
OBS:=$(patsubst src/%.c,$(BUILDDIR)/%.o,$(patsubst src/%.cpp,$(BUILDDIR)/%.o,$(SRC)))
INC:=$(wildcard include/*.h)
ALLDEPS:=Makefile

all:	$(PROG) x xreportSize

$(PROG):	 $(OBS) $(INC) $(ALLDEPS) | $(PROGDIR)
	g++ $(DEBUGFLAG) $(OBS) $(UMD_LIBDIRS) $(UMD_LIBS) $(UMD_DLLPATHS) -o $@

x:	FORCE
	make -C cross

x%:	FORCE
	make -C cross $*

$(BUILDDIR):
	mkdir -p $@

$(BUILDDIR)/%.o:	src/%.c $(INC) | $(BUILDDIR)
	gcc $(DEBUGFLAG) -c $< -Iinclude $(UMD_INCLUDES) -o $@

$(BUILDDIR)/%.o:	src/%.cpp $(INC) | $(BUILDDIR)
	g++ $(DEBUGFLAG) -c $< -Iinclude $(UMD_INCLUDES) -o $@

$(PROGDIR):
	mkdir -p $@

realclean:	clean
	make -C cross realclean
	rm -rf $(BUILDDIR)
	rm -f $(PROGDIR)/*


clean:	FORCE
	make -C cross clean
	rm -f $(BUILDDIR)/*

#./ethdump --out=tt.pcap --generate-traffic --loopback-mode=2
run:	x $(PROG) 
	tt-smi -r >/dev/null 2>&1
	$(PROG)


.PHONY:	FORCE
