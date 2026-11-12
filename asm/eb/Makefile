UTILS=../../utils
EBASIC_ROM = ../../roms/ebasic.bin

all: $(EBASIC_ROM)

$(EBASIC_ROM): ebasic.ihx
	$(UTILS)/ihx2bin <$< | dd bs=4k iflag=fullblock conv=sync of=$@

%.ihx: %.rel
	sdldz80 -n -i $^

%.rel: %.as ../aquarius.mac
	sdasz80 -p -l -s -o -I.. $<

clean:
	rm -f *.{sym,lst,rel,ihx}

distclean: clean
	rm -f  $(EBASIC_ROM)
