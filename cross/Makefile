export CROSSDIR=$(CURDIR)
export BASEDIR=$(abspath $(CROSSDIR)/..)

include config/Makevars.mk

export ALLDEP=Makefile $(wildcard config/*.mk ../shared/include/*.h ../shared/include/*.tcc)

PROG:=test10

all:	bin/$(PROG).bin build/include/$(PROG)-exports.h
	@echo Done.

build/include/$(PROG)-exports.h:	build/$(PROG).elf
	@mkdir -p build/include
	@$(OBJDUMP) -C -t $^ | perl -n -e 'BEGIN {print "// GENERATED ".`date`."#pragma once\n#include \"itype.h\"\nnamespace MFM::T6 {\n"} END { print "}\n"} /^([0-9a-fA-F]+).*?__(transportblock_.*?)$$/ && print "  static const u32 $$2 = 0x$$1;\n"' > $@

include config/Makebin.mk

.PHONY:	FORCE
