STARTFILE:=src/_BUD.S
LINKERSCRIPT:=src/_BUD.ld.in
MARCH:=rv32ima_zicsr_zba_zbb
CXXFLAGS+=-Iinclude -I../shared/include
#CXXFLAGS+=-march=$(MARCH) -ffreestanding -nostdlib -fno-exceptions -fno-rtti
CXXFLAGS+=-march=$(MARCH) -ffreestanding -nostdlib -fno-rtti
CXXFILES:=$(wildcard src/*.cpp)
CXXFILESSHARED:=$(../shared/src/*.cpp)
OFILES:=$(patsubst src/%.cpp,build/%.o,$(CXXFILES))
OFILES+=$(patsubst ../shared/src/%.cpp,build/%.o,$(CXXFILESSHARED))
HFILES:=$(wildcard include/*.h)
LDPATH:=build/gen.ld

$(LDPATH):	$(LINKERSCRIPT) $(HFILES) $(ALLDEP) | build
	echo HARO
	$(CXX) $(CXXFLAGS) -E -P -x c++ $< -o $@

# $(PROG).o:	$(PROG).cpp $(HFILES) FORCE
# 	echo ($(HFILES))
# 	$(CXX) $(CXXFLAGS) -c $< -o $@

build/%.o:	src/%.cpp $(HFILES) $(ALLDEP) | build
	$(CXX) $(CXXFLAGS) -c $< -o $@ -save-temps

build/$(PROG).elf:	$(OFILES) $(STARTFILE) $(LDPATH) $(ALLDEP) | build
	$(CXX) -o $@ $(OFILES) $(STARTFILE) $(CXXFLAGS) -T$(LDPATH) -save-temps

bin/$(PROG).bin:	build/$(PROG).elf | bin reportSize
	$(OBJCOPY) -O binary $< $@

build:	FORCE
	@mkdir -p build

bin:	FORCE
	@mkdir -p bin

reportSize:	build/$(PROG).elf
	$(SIZE) $^

dumpElf:	build/$(PROG).elf
	$(OBJDUMP) -C -d -r -h $^

dumpBin:	bin/$(PROG).bin
	$(OBJDUMP) -C -D -b binary -m riscv $^

clean:	FORCE
	rm -f *~ *.o

realclean:	clean
	rm -rf build
	rm -f bin/*
