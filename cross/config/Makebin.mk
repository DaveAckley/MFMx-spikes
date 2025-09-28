STARTFILE:=src/_BUD.S
LINKERSCRIPT:=src/_BUD.ld.in
MARCH:=rv32ima_zicsr_zba_zbb
CXXFLAGS+=-Iinclude -I../include
CXXFLAGS+=-march=$(MARCH) -ffreestanding -nostdlib -fno-exceptions -fno-rtti
CXXFILES:=$(wildcard src/*.cpp)
HFILES:=$(wildcard include/*.h)
LDPATH:=build/gen.ld

$(LDPATH):	$(LINKERSCRIPT) $(HFILES) $(ALLDEP) | build
	$(CXX) $(CXXFLAGS) -E -P -x c++ $< -o $@

# $(PROG).o:	$(PROG).cpp $(HFILES) FORCE
# 	echo ($(HFILES))
# 	$(CXX) $(CXXFLAGS) -c $< -o $@

build/%.o:	src/%.cpp $(HFILES) $(ALLDEP) | build
	$(CXX) $(CXXFLAGS) -c $< -o $@

build/$(PROG).elf:	build/$(PROG).o $(STARTFILE) $(LDPATH) $(ALLDEP) | build
	$(CXX) -o $@ $< $(STARTFILE) $(CXXFLAGS) -T$(LDPATH)

bin/$(PROG).bin:	build/$(PROG).elf | bin
	$(OBJCOPY) -O binary $< $@

build:	FORCE
	mkdir -p build

bin:	FORCE
	mkdir -p bin

reportSize:	build/$(PROG).elf
	$(SIZE) $^

dumpElf:	build/$(PROG).elf
	$(OBJDUMP) -C -D $^

dumpBin:	bin/$(PROG).bin
	$(OBJDUMP) -C -D -b binary -m riscv $^

clean:	FORCE
	rm -f *~ *.o

realclean:	clean
	rm -rf build
	rm -f bin/*
