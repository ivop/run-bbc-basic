MADS=mads

top.rom: toprom.s
	$(MADS) -o:$@ $<


clean:
	rm -f top.rom

cleaner: clean
	rm -f *~
