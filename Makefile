
all: patch compile

patch:
	sh patch.sh

kernelconf: patch
	sh conf.sh

compile: kernelconf
	make -C src

install:
	make -C src
	make -C src install
	make -C man install
	sh magic.sh

uninstall:
	make -C src uninstall
	make -C man uninstall

mrproper:
	rm -f *~
	rm -f doc/*~
	rm -f man/*~
	rm -f patches/*~
	make -C src mrproper

dist:
	sh mkdist.sh
