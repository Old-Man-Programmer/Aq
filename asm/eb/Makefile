UTILS=../../utils
EBASIC_ROM = ../../roms/ebasic.bin
ORIG_EBASIC_ROM = ../../roms/ExtendedBasic.bin

all: $(EBASIC_ROM)

$(EBASIC_ROM): ebasic.ihx
	$(UTILS)/ihx2bin <$< | dd bs=4k iflag=fullblock conv=sync of=$@

%.ihx: %.rel
	sdldz80 -n -i $^

%.rel: %.as ../aquarius.mac
	sdasz80 -p -l -s -o -I.. $<

# Avoid copyright infringement
ebasic.as: ebasic.xdelta $(ORIG_EBASIC_ROM) ../../utils/xor
	# Note: If your ROM or z80dis version is different, this will fail!
	# ROM is 16K, but it's just 2 copies of Extended BASIC.  Use last one.
	dd if=$(ORIG_EBASIC_ROM) of=eb.bin skip=0 bs=4k count=1
	# decrypt
	../../utils/xor eb.bin 220 >eb.bin.dec
	# 1st 16 bytes are not encrypted
	dd if=eb.bin conv=notrunc of=eb.bin.dec bs=16 count=1
	z80dasm -a -l -g 0xe000 -t eb.bin.dec >ebasic.dis
	xdelta patch ebasic.xdelta ebasic.dis ebasic.as

clean:
	# Note: this will wipe any changes made to ebasic.as!
	rm -f *.{sym,lst,rel,ihx} eb.bin eb.bin.dec ebasic.{dis,as}

distclean: clean
	rm -f $(EBASIC_ROM)
