#!/bin/sh

if [ ! -d /usr/src/linux ];
then
	echo "No linux sources found in /usr/src/linux, aborting."
	exit 1
fi

head -3 /usr/src/linux/Makefile | tr -d " " > /tmp/help.$$
source /tmp/help.$$
rm /tmp/help.$$
VPS=`echo "$VERSION.$PATCHLEVEL.$SUBLEVEL"`
VP=`echo "$VERSION.$PATCHLEVEL"`

if [ -d /usr/src/linux/fs/dmsdos ];
then
	echo "dmsdos is already present in your kernel souce tree."
        echo "This may have one of the following reasons:"
	echo " * You have already run this script. There's no reason to run it again."
	echo " * You have an older dmsdos version installed in your kernel sources. Get"
	echo "   fresh kernel sources and retry."
	exit 1
fi

echo "This script integrates dmsdos into your kernel source tree. This will melt"
echo "dmsdos and the linux kernel sources together. It will allow you to compile" 
echo "dmsdos as a fix part of your kernel (probably useful for a boot disk)."
echo "Be warned, this is not well tested. If you haven't read the installation"
echo "instructions or if you don't know what you are doing press CTRL-C now."
echo
echo "WARNING:"
echo "THIS MAY MESS UP YOUR KERNEL SOURCES. BE SURE TO HAVE A BACKUP."
echo 
echo "Press Enter to continue. Press CTRL-C to abort."
read junk

echo -n "Scanning for *.rej files..."
REJ=`( cd /usr/src/linux; find . -name "*.rej" -print )`
if [ ! -z "$REJ" ];
then
        echo
        echo "There are *.rej files lying around in your kernel sources. Can't continue."
        echo "$REJ"
        echo "This usually means that you tried to patch your kernel some time ago but"
        echo "that failed horribly. If you haven't repaired this by hand your kernel"
        echo "sources are screwed up. Either throw them away and install fresh sources or"
        echo "just delete the *.rej files in order to make me believe everything is ok :)"
        exit 1
fi

echo " not found, ok."


mkdir /usr/src/linux/fs/dmsdos
( cd src
  cp dblspace_*.c dstacker_*.c dmsdos.h Config.in /usr/src/linux/fs/dmsdos 
  cp Makefile.kernel /usr/src/linux/fs/dmsdos/Makefile
  if [ "$VP" = "2.0" ];
  then
	F=dblspace_fileops.c-2.0.33
  else
	F=dblspace_fileops.c-2.1.80
  fi
  cp $F /usr/src/linux/fs/dmsdos/dblspace_fileops.c
  cp /usr/src/linux/Documentation/Configure.help /usr/src/linux/Documentation/Configure.help.orig
  cat Configure.help >> /usr/src/linux/Documentation/Configure.help
)

( cd /usr/src/linux; patch -p1 ) < patches/in-kernel-2.0.35.diff

( cd /usr/src/linux/fs/dmsdos
  ln -s ../../include/linux/config.h dmsdos-config.h
)

echo -n "Scanning for *.rej files..."
REJ=`( cd /usr/src/linux; find . -name "*.rej" -print )`
if [ ! -z "$REJ" ];
then
        echo
        echo "There were *.rej files created during patch. Can't continue."
        echo "$REJ"
        echo "This shouldn't happen unless you have a highly patched or hacked kernel."
        echo "Please have a look at the files and find out what went wrong."
        exit 1
fi

echo " not found, ok."
