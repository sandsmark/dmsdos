
all: patch compile

patch:
	sh patch.sh

kernelconf: patch
	sh conf.sh

compile: kernelconf
	make -C src

install: compile
	make -C src install
	sh magic.sh

uninstall:
	make -C src uninstall

mrproper:
	rm -f *~
	rm -f doc/*~
	make -C src mrproper
