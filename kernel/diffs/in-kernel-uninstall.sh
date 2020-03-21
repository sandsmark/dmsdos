#!/bin/sh

if [ ! -d /usr/src/linux ];
then
	echo "No linux sources found in /usr/src/linux, aborting."
	exit 1
fi

if [ ! -d /usr/src/linux/fs/dmsdos ];
then
	echo "dmsdos is not present in your kernel souce tree."
	exit 1
fi

rm -r /usr/src/linux/fs/dmsdos
( cd /usr/src/linux/fs
  mv Config.in.orig Config.in
  mv Makefile.orig Makefile
  mv filesystems.c.orig filesystems.c
  mv /usr/src/linux/Documentation/Configure.help.orig /usr/src/linux/Documentation/Configure.help
)
