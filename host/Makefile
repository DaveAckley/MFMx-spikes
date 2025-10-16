include config/Makevars.mk

DEBUGFLAG:=-g
PROGDIR:=./bin
PROG:=$(PROGDIR)/$(PROGNAME)
BUILDDIR:=./build
HOSTBUILDDIR:=$(BUILDDIR)/host
SHAREDBUILDDIR:=$(BUILDDIR)/shared
BUILDDIRS:=$(HOSTBUILDDIR) $(SHAREDBUILDDIR)

SHAREDCPPSRC:=$(wildcard ../shared/src/*.cpp)
SHAREDCPPOBS:=$(patsubst ../shared/src/%.cpp,$(BUILDDIR)/shared/%.o,$(SHAREDCPPSRC))

#HOSTCSRC:=$(wildcard src/*.c)
HOSTCPPSRC:=$(wildcard src/*.cpp)
#HOSTCOBS:=$(patsubst src/%.c,$(BUILDDIR)/%.o,$(HOSTCSRC)))
HOSTCPPOBS:=$(patsubst src/%.cpp,$(BUILDDIR)/host/%.o,$(HOSTCPPSRC))

OBS:= $(SHAREDCPPOBS) $(HOSTCPPOBS)

INC:=$(wildcard include/*.h)
export ALLDEP=Makefile $(wildcard config/*.mk ../shared/include/*.h ../shared/include/*.tcc)

all:	$(BUILDDIRS) $(PROGDIR) $(PROG) #xreportSize

$(PROG):	 $(OBS) $(INC) $(ALLDEP) | $(PROGDIR)
	g++ $(DEBUGFLAG) -O2 $(OBS) $(UMD_LIBDIRS) $(UMD_LIBS) $(UMD_DLLPATHS) -o $@

$(HOSTBUILDDIR) $(SHAREDBUILDDIR) $(PROGDIR):	FORCE
	mkdir -p $@

$(SHAREDBUILDDIR)/%.o:	../shared/src/%.cpp $(INC) $(ALLDEP) | $(BUILDDIR)
	g++ $(DEBUGFLAG) -c -O2 $< -Iinclude -I../shared/include $(UMD_INCLUDES) -o $@

$(HOSTBUILDDIR)/%.o:	src/%.cpp $(INC) $(ALLDEP) | $(BUILDDIR)
	g++ $(DEBUGFLAG) -c -O2 $< -Iinclude -I../shared/include -I../cross/build/include $(UMD_INCLUDES) -o $@

realclean:	clean
	rm -rf $(BUILDDIRS)
	rm -f $(PROGDIR)/*

clean:	FORCE
	rm -f *~ include/*~ src/*~
	rm -f $(BUILDDIR)/*.o

#./ethdump --out=tt.pcap --generate-traffic --loopback-mode=2
run:	$(PROG) 
	tt-smi -r >/dev/null 2>&1
	$(PROG) 2>&1


.PHONY:	FORCE
