export CROSSDIR=$(CURDIR)
export BASEDIR=$(abspath $(CROSSDIR)/..)

include config/Makevars.mk

export ALLDEP=Makefile $(wildcard config/*.mk)

PROG:=test10

all:	bin/$(PROG).bin
	@echo didit done

include config/Makebin.mk

.PHONY:	FORCE
