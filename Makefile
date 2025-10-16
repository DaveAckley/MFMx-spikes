ALLCMDS:=all clean realclean

$(ALLCMDS):	FORCE 
	make $@-here
	make -C shared $@
	make -C cross $@
	make -C host $@

all-here:	FORCE

realclean-here:	clean-here

clean-here:	FORCE
	rm -f *~

run:	all
	make -C host run

.PHONY:	FORCE
