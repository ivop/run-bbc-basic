CC=gcc
#CFLAGS=-g3 -Wall -Wextra
CFLAGS=-O3 -flto -Wall -Wextra

runbasic: main.c fake6502/fake6502.c
	$(CC) $(CFLAGS) -o $@ $^

basic.inc: roms/basic310hi.rom
	xxd -i $< > $@

toprom.inc: toprom/top.rom
	xxd -i $< > $@

clean:
	rm -f runbasic

cleaner: clean
	rm -f *~
