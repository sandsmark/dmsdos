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

if [ "$VERSION" -lt 2 ];
then
	echo "Kernel version $VPS is not supported. Need 2.0.29 or newer from 2.0 series"
	echo "or 2.1.80 or newer."
	exit 1
fi

echo "Now I'm trying to find out whether your kernel needs some patches for dmsdos."
echo "If something is patched, a log file '*.log' will be written to /tmp."

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

if [ -f /usr/src/linux/fs/fat/cvf.c ];
then
	echo "Kernel has already CVF-FAT support."
	grep CVF-FAT-VERSION-ID /usr/src/linux/fs/fat/cvf.c > /dev/null
	if [ $? = 1 ];
	then
		echo "But the CVF-FAT version in your kernel is an older version."
		echo "You may want to update it. I will _not_ do this for you."
                echo "See the installation instructions for details."
		echo
		echo "Press Enter to continue."
		read junk
	fi
else
	if [ "$VP" = "2.0" ];
	then
		if [ "$SUBLEVEL" -lt 29 ];
		then
			echo "Kernel version $VPS is not supported. Need 2.0.29 or newer from 2.0 series"
			echo "or 2.1.80 or newer."
		fi
		#check for fat32
		if [ -f /usr/src/linux/fs/nls.c ];
		then
			echo "Kernel 2.0.$SUBLEVEL with FAT32 support detected."
		    if [ $SUBLEVEL -lt 36 ];
		    then
			echo "WARNING: FAT32 kernels earlier than 2.0.36 have a serious rmdir bug"
			echo "that causes filesystem corruption in a msdos filesystem. This is not"
			echo "a dmsdos problem but it is known to damage compressed filesystems too."
			echo "Please upgrade to 2.0.36 or apply the msdos-rmdir-bugfix.diff manually"
			echo "if you haven't repaired the bug otherwise. (I will not fix this bug.)"
			echo "Press Enter to continue."
			read junk
		    fi
			echo "I'm going to install the CVF-FAT patches for FAT32 now."
			echo "If you do not want me to do this, press CTRL-C now."
			echo "To continue, press enter."
			read junk
			( cd /usr/src/linux; patch -p1 ) < patches/cvf.diff-2.0.33+fat32-0.2.8 >& /tmp/cvf.diff.log
		else
			echo "Kernel 2.0.$SUBLEVEL without FAT32 detected."
			echo "I'm going to install the CVF-FAT patches now."
			echo "If you do not want me to do this, press CTRL-C now."
			echo "WARNING: If you want to use FAT32 please abort now and"
			echo "install the FAT32 patches before installing dmsdos."
			echo "To continue, press enter."
			read junk
			( cd /usr/src/linux; patch -p1 ) < patches/cvf.diff-2.0.33 >& /tmp/cvf.diff.log
		fi
		if [ -d /usr/src/linux/fs/uvfat ];
		then
			echo "UVFAT patches found."
			echo "I'm going to install additional CVF-FAT patches for UVFAT now."
			echo "If you do not want me to do this, press CTRL-C now."
			echo "To continue, press enter."
			read junk
			( cd /usr/src/linux; patch -p1 ) < patches/cvf-uvfat.diff-2.0.33 >& /tmp/cvf-uvfat.diff.log
		fi
	else
		if [ "$VP" = "2.1" -a "$PATCHLEVEL" -lt 80 ];
		then
			echo "Kernel version $VPS is not supported. Need 2.0.29 or newer from 2.0 series"
			echo "or 2.1.80 or newer."
			exit 1
		fi
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
