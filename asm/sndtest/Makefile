UTILS=../../utils

TESTS = car lowc shot2 siren whis expl scale shot snd440

BAS = $(patsubst %,%.bas,$(TESTS))

all: tape
basic: $(BAS)

%.bas: %.as %.ihx

%.ihx: %.rel
	sdldz80 -n -i $^

%.rel: %.as ../aquarius.mac
	sdasz80 -s -l -p -o -I.. $<

%.bas: %.ihx $(UTILS)/asm2bas $(UTILS)/ihx2bin
	$(UTILS)/ihx2bin -b <$*.ihx >$@
	$(UTILS)/asm2bas $*

tape: $(BAS) ../../Aq
	rm -f $@
	trap "rm -f /tmp/$$$$.bas" 0; \
	for x in $(filter %.bas,$^); do \
	  sed 's/^RUN$$/CSAVE"'$${x%.bas}'"\n\nNEW/' $$x; \
	done >/tmp/$$$$.bas; \
	echo "POKE14339,118:v=USR(0)" >>/tmp/$$$$.bas; \
	(cd ../..; ./Aq -B -t asm/sndtest/$@ -b /tmp/$$$$.bas)

clean:
	rm -f *.{ihx,lst,rel,sym}

distclean: clean
	rm -f $(BAS) tape
