#!/bin/sh

if [ ! -f /usr/src/linux/Makefile ];
then
	echo "No Linux sources found in /usr/src/linux."
	echo "Sorry, I cannot determine automatically what needs to be done."
	echo "Please install dmsdos manually."
	echo "See the installation instructions for details."
	exit 1
fi

head -3 /usr/src/linux/Makefile | tr -d " " > /tmp/help.$$
source /tmp/help.$$
rm /tmp/help.$$
VPS=`echo "$VERSION.$PATCHLEVEL.$SUBLEVEL"`
VP=`echo "$VERSION.$PATCHLEVEL"`

echo "Now I'm trying to find out whether your kernel needs some patches for dmsdos."
echo "If something is patched, a log file '*.log' will be written to /tmp."
echo -n "Removing possibly old *.rej files..."
( cd /usr/src/linux; find . -name "*.rej" -exec rm {} \; )
echo " ok."

if [ -f /usr/src/linux/fs/fat/cvf.c ];
then
	echo "Kernel has already CVF-FAT support."
else
	if [ "$VP" = "2.0" ];
	then
		#check for fat32
		if [ -f /usr/src/linux/fs/nls.c ];
		then
			echo "Kernel 2.0.$SUBLEVEL with FAT32 support detected."
			echo "I'm going to install the CVF-FAT patches for FAT32 now."
			echo "If you do not want me to do this, press CTRL-C now."
			echo "To continue, press enter."
			read junk
			( cd /usr/src/linux; patch -p1 ) < cvf.diff-2.0.33+fat32-0.2.8 >& /tmp/cvf.diff.log
		else
			echo "Kernel 2.0.$SUBLEVEL without FAT32 detected."
			echo "I'm going to install the CVF-FAT patches now."
			echo "If you do not want me to do this, press CTRL-C now."
			echo "WARNING: If you want to use FAT32 please abort now and"
			echo "install the FAT32 patches before installing dmsdos."
			echo "To continue, press enter."
			read junk
			( cd /usr/src/linux; patch -p1 ) < cvf.diff-2.0.33 >& /tmp/cvf.diff.log
		fi
		if [ -d /usr/src/linux/fs/uvfat ];
		then
			echo "UVFAT patches found."
			echo "I'm going to install additional CVF-FAT patches for UVFAT now."
			echo "If you do not want me to do this, press CTRL-C now."
			echo "To continue, press enter."
			read junk
			( cd /usr/src/linux; patch -p1 ) < cvf-uvfat.diff-2.0.33 >& /tmp/cvf-uvfat.diff.log
		fi
	else
		if [ "$VP" != "2.1" ];
		then
			echo "could not detect kernel version"
			exit 1
		fi
		if [ $PATCHLEVEL -lt 80 ];
		then
			echo "kernel version not supported, need >= 2.1.80"
			exit 1
		fi
		if [ $PATCHLEVEL -gt 79 ];
		then
			echo "kernel >= 2.1.80, no patch needed"
		fi
	fi
fi

#search for kerneld-autoload hack
echo "Checking for kerneld-autoload hack..."
grep 'request_module(\"dmsdos\")' /usr/src/linux/fs/fat/inode.c > /dev/null
if [ $? = 0 ];
then
	echo "ERROR: old kerneld-autoload hack in inode.c detected."
	echo "Can't continue. Please remove that."
	echo "See the installation instructions for details."
	exit 1
fi

grep 'request_module(\"dmsdos\")' /usr/src/linux/fs/fat/cvf.c > /dev/null
if [ $? = 0 ];
then
	echo "kerneld-autoload diff is already present."
else
	echo "kerneld-autoload diff is not installed."
	echo "If you want to have kerneld automatically load dmsdos you need"
	echo "this patch. If you want me to install the patch now, say Y and"
	echo "press Enter. You can at any time later install the patch manually."
	echo "If you do not know what you are doing, say N and press Enter."
	echo "See the installation instructions for details."
	echo
	echo -n "Do you want me to install the kerneld-autoload patch now ? (Y/N) "
	read ANS junk
	if [ "$ANS" = y -o "$ANS" = Y ];
	then
		( cd /usr/src/linux; patch -p1 ) < kerneld-autoload.diff >& /tmp/kerneld-autoload.diff.log
		#this one must be recompiled later :)
		rm -f /usr/src/linux/fs/fat/cvf.o
	fi
fi

echo -n "Scanning for *.rej files..."
REJ=`( cd /usr/src/linux; find . -name "*.rej" -print )`
if [ ! -z "$REJ" ];
then
	echo
	echo "There were *.rej files created during patch. Can't continue."
	echo "$REJ"
	echo "This shouldn't happen unless you have a highly patched or hacked kernel :)"
	echo "Please have a look at the files and find out what went wrong."
	exit 1
fi

echo " not found, ok."
exit 0
