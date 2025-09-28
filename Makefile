PROGNAME:=$(notdir $(CURDIR))
PROGDIR:=./bin
PROG:=$(PROGDIR)/$(PROGNAME)
BUILDDIR:=./build
SRC:=$(wildcard src/*.cpp src/*.c)
OBS:=$(patsubst src/%.c,$(BUILDDIR)/%.o,$(patsubst src/%.cpp,$(BUILDDIR)/%.o,$(SRC)))
INC:=$(wildcard include/*.h)
ALLDEPS:=Makefile

all:	$(PROG)

$(PROG):	 $(OBS) $(INC) $(ALLDEPS) | $(PROGDIR)
	g++ $(OBS) -o $@

$(BUILDDIR):
	mkdir -p $@

$(BUILDDIR)/%.o:	src/%.c $(INC) | $(BUILDDIR)
	gcc -c -O2 $< -Iinclude -o $@

$(BUILDDIR)/%.o:	src/%.cpp $(INC) | $(BUILDDIR)
	g++ -c -O2 $< -Iinclude -o $@

$(PROGDIR):
	mkdir -p $@

realclean:	clean
	rm -rf $(BUILDDIR)
	rm -f $(PROGDIR)/*


clean:	FORCE
	rm -f $(BUILDDIR)/*

#./ethdump --out=tt.pcap --generate-traffic --loopback-mode=2
run:	$(PROG)
	$(PROG) --hwinfo


.PHONY:	FORCE
