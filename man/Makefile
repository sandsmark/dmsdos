
MAN_PREFIX=/usr/local/man

install:
	install dutil.1 -m 644 $(MAN_PREFIX)/man1/dutil.1
	install dmsdosd.1 -m 644 $(MAN_PREFIX)/man1/dmsdosd.1
	install dmsdosfsck.8 -m 644 $(MAN_PREFIX)/man8/dmsdosfsck.8
	install mcdmsdos.1 -m 644 $(MAN_PREFIX)/man1/mcdmsdos.1
	install cvftest.1 -m 644 $(MAN_PREFIX)/man1/cvftest.1
	install cvflist.1 -m 644 $(MAN_PREFIX)/man1/cvflist.1

install-min:
	install dutil.1 -m 644 $(MAN_PREFIX)/man1/dutil.1

uninstall:
	rm -f $(MAN_PREFIX)/man1/dutil.1
	rm -f $(MAN_PREFIX)/man1/dmsdosd.1
	rm -f $(MAN_PREFIX)/man8/dmsdosfsck.8
	rm -f $(MAN_PREFIX)/man1/mcdmsdos.1
	rm -f $(MAN_PREFIX)/man1/cvftest.1
	rm -f $(MAN_PREFIX)/man1/cvflist.1
