TAPE = tape

BAS = \
  bb.bas \
  boxes.bas \
  chars.bas \
  chr-poke.bas \
  clk.bas \
  clk2.bas \
  col-poke.bas \
  colors.bas \
  conccirc.bas \
  hangman.bas \
  kaleid.bas \
  palette.bas \
  pt.bas \
  rest-ex.bas \
  rndmul.bas \
  snd-test.bas


all: $(TAPE)

clean:

distclean:
	rm -f $(TAPE)

$(TAPE): $(BAS) ../Aq
	rm -f $@
	trap "rm -f /tmp/$$$$.bas" 0; \
	for x in $(filter %.bas,$^); do \
	  sed 's/^RUN$$/CSAVE"'$${x%.bas}'"\n\nNEW/' $$x; \
	done >/tmp/$$$$.bas; \
	echo "POKE14339,118:v=USR(0)" >>/tmp/$$$$.bas; \
	(cd ..; ./Aq -B -t bas/$@ -b /tmp/$$$$.bas)
