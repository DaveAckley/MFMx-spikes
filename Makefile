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
	g++ $(DEBUGFLAG) -O2 $(OBS) -o $@

x:	FORCE
	make -C cross

x%:	FORCE
	make -C cross $*

$(BUILDDIR):
	mkdir -p $@

$(BUILDDIR)/%.o:	src/%.c $(INC) | $(BUILDDIR)
	gcc $(DEBUGFLAG) -c -O2 $< -Iinclude -o $@

$(BUILDDIR)/%.o:	src/%.cpp $(INC) | $(BUILDDIR)
	g++ $(DEBUGFLAG) -c -O2 $< -Iinclude -o $@

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
