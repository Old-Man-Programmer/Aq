UTILS=../../utils
TAPE = zexall.tape
ROM = ../../roms/zexall.bin

all: $(TAPE) $(ROM)

zexall-%.rel: ../do%.as zexall.z80 crctab.mac ../aquarius.mac
	sdasz80 -s -l -p -o -I.. $@ $(filter-out %.mac,$^)

%.ihx: %.rel
	sdldz80 -n -i $^

$(TAPE): zexall-ram.ihx $(UTILS)/ihx2bin
	$(UTILS)/ihx2bin -t zexall <zexall-ram.ihx >zexall.tape

$(ROM): zexall-rom.ihx $(UTILS)/ihx2bin
	$(UTILS)/ihx2bin <$< | dd bs=8k iflag=fullblock conv=sync of=$@
#	ls -l $@

clean:
	rm -f *.{ihx,lst,sym,rel}

distclean: clean
	rm -f $(TAPE) $(ROM)
