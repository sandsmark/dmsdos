#!/bin/sh

if [ ! -d /usr/src/linux ];
then
	echo "Linux sources not found in /usr/src/linux."
	echo "Sorry, I cannot determine automatically what needs to be done."
	echo "Please read the installation instructions for details."
	exit 1
fi

CT=config
if [ -f /usr/src/linux/scripts/lxdialog/lxdialog ];
then
CT=menuconfig
fi

if [ ! -f /usr/src/linux/.config ];
then
	echo "Your kernel seems not configured."
	echo "I'm going to call kernel configuration now."
	echo "If you are very sure this need not be done press CTRL-C now"
	echo "and install dmsdos manually. To continue, press Enter."
	read junk
	( cd /usr/src/linux; make $CT )
fi

CONFIG_MODULES=n
CONFIG_BLK_DEV_LOOP=n
CONFIG_FAT_FS=n

source /usr/src/linux/.config

if [ "$CONFIG_MODULES" != "n" -a "$CONFIG_BLK_DEV_LOOP" != "n" -a "$CONFIG_FAT_FS" != "n" -a -f /usr/src/linux/fs/fat/cvf.o ];
then
	echo "This really looks like you have already configured and compiled your" 
	echo "kernel correctly for CVF-FAT. I hope I'm not wrong here :-)"
	exit 0
fi

while [ "$CONFIG_MODULES" = "n" -o "$CONFIG_BLK_DEV_LOOP" = "n" -o "$CONFIG_FAT_FS" = "n" ];
do
	echo "Your kernel is not configured correctly for CVF-FAT."
	echo "The following feature(s) is(are) missing:"
if [ "$CONFIG_MODULES" = "n" ];
then
	echo " - loadable module support"
fi
if [ "$CONFIG_BLK_DEV_LOOP" = "n" ];
then
	echo " - loopback block device"
fi
if [ "$CONFIG_FAT_FS" = "n" ];
then
	echo " - fat filesystem"
fi
	echo "Please correct this now. I'm going to call kernel configuration now."
	echo "If you don't want me to do this, press CTRL-C now. To continue, press Enter."
	read junk
	( cd /usr/src/linux; make $CT )

	CONFIG_MODULES=n
	CONFIG_BLK_DEV_LOOP=n
	CONFIG_FAT_FS=n

	source /usr/src/linux/.config
done

echo
echo "I'm going to recompile your kernel and your modules now."
echo "If you are very sure this need not be done, press CTRL-C now"
echo "and install dmsdos manually. "
echo
echo "Note that I will *not* install the new kernel and the new modules."
echo "I'll just compile them. For safety, installation is up to you :)"
echo
echo "To continue, press Enter."
read junk

make -C /usr/src/linux dep
make -C /usr/src/linux clean
make -C /usr/src/linux zImage || make -C /usr/src/linux bzImage
if [ "$?" != "0" ];
then
	echo "kernel recompile FAILED!!! I must give up now."
	echo "Go ahead and try to find out what's wrong here."
	exit 1
fi
make -C /usr/src/linux modules
if [ "$?" != "0" ];
then
        echo "module recompile FAILED!!! I must give up now."
        echo "Go ahead and try to find out what's wrong here."
        exit 1
fi

echo
echo "Okay, that seems to have succeeded. Just remember to install your new"
echo "kernel and modules with 'make zlilo' and 'make modules_install' or"
echo "similar commands in /usr/src/linux, and reboot. I'm just a dumb script"
echo "and the person who wrote me made me refuse to do this. :-)"
echo
echo "So, the following error is intended. Just do what needs to be done and then"
echo "retry. The next time I should recognize your new setup and continue with"
echo "dmsdos compilation."
echo 
echo "You can even continue without installing the new kernel and the new modules"
echo "and without rebooting. Dmsdos will compile in spite of that, but it may"
echo "not work correctly or not run at all unless you have rebooted with the new"
echo "kernel and the new modules."
exit 1
