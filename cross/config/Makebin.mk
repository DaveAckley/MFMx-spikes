#STARTFILE:=src/_BUD.S
LINKERSCRIPT:=src/_BUD.ld.in
MARCH:=rv32ima_zicsr_zba_zbb
CXXFLAGS+=-Iinclude -I../shared/include
CXXFLAGS+=-march=$(MARCH) -ffreestanding -nostdlib -fno-exceptions -fno-rtti
#CXXFLAGS+=-march=$(MARCH) -ffreestanding -nostdlib -fno-exceptions -fno-rtti
#CXXFLAGS+=-march=$(MARCH) -ffreestanding -nostdlib -fno-rtti
SFILES:=$(wildcard src/*.S)
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

build/$(PROG).elf:	$(OFILES) $(SFILES) $(LDPATH) $(ALLDEP) | build
	$(CXX) -o $@ $(OFILES) $(SFILES) $(CXXFLAGS) -Wl,--no-relax -T$(LDPATH) -lgcc -save-temps

bin/$(PROG).bin:	build/$(PROG).elf | bin dissElf reportSize
	$(OBJCOPY) -O binary $< $@

build:	FORCE
	@mkdir -p build

bin:	FORCE
	@mkdir -p bin

reportSize:	build/$(PROG).elf
	$(SIZE) $^ | tail -1 | echo $$(date '+%s ') $$(cat -) | echo $$(cat -) $$(date '+"%c" ') | tee -a "bin/$(PROG)-builds.dat"

dumpElf:	build/$(PROG).elf
	$(OBJDUMP) -C -d -r -h $^

dissElf:	build/$(PROG).elf
	$(OBJDUMP) --disassemble-all -C $^ >$^.asm

dumpBin:	bin/$(PROG).bin
	$(OBJDUMP) -C -D -b binary -m riscv $^

clean:	FORCE
	rm -f *~ *.o build/*~ bin/*~

realclean:	clean
	rm -rf build
	rm -f bin/$(PROG).bin  # LEAVE -builds.dat!

destroydataclean:	realclean
	rm -rf bin
