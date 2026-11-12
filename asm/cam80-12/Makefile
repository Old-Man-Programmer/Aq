UTILS=../../utils
ROMS=../../roms

all: cam80-12.tape $(ROMS)/cam80-12.bin

cam80-12-%.rel: ../do%.as camel80.azm ../aquarius.mac
	sdasz80 -z -s -l -p -o -I.. $@ $(filter-out %.mac,$^)

%.ihx: %.rel
	sdldz80 -i $^

cam80-12.tape: cam80-12-ram.ihx $(UTILS)/ihx2bin
	$(UTILS)/ihx2bin -t cam80-12 <cam80-12-ram.ihx >cam80-12.tape

$(ROMS)/cam80-12.bin: cam80-12-rom.ihx $(UTILS)/ihx2bin
	$(UTILS)/ihx2bin <$< | dd bs=8k iflag=fullblock conv=sync of=$@
#	ls -l $@

clean:
	rm -f *.{ihx,lst,sym,rel}

distclean: clean
	rm -f cam80-12.tape $(ROMS)/cam80-12.bin
