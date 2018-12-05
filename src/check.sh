#!/bin/sh

echo "Checking kernel configuration..."

if [ ! -f /usr/src/linux/Makefile ];
then
        echo "Hmm, no Linux sources found in /usr/src/linux"
        exit 0
fi

head -3 /usr/src/linux/Makefile | tr -d " " > /tmp/help.$$
source /tmp/help.$$
rm /tmp/help.$$
VPS=`echo "$VERSION.$PATCHLEVEL.$SUBLEVEL"`
VP=`echo "$VERSION.$PATCHLEVEL"`

if [ ! -f /usr/src/linux/.config ];
then
	echo "Hmm, kernel is not configured."
	exit 0
fi

CONFIG_MODULES=n
CONFIG_BLK_DEV_LOOP=n
CONFIG_FAT_FS=n
CONFIG_UMSDOS_FS=n

source /usr/src/linux/.config

WARN=n

if [ "$CONFIG_MODULES" = "n" -o "$CONFIG_BLK_DEV_LOOP" = "n" -o "$CONFIG_FAT_FS" = "n" ];
then
	echo
	echo "WARNING: Your kernel is not configured correctly for dmsdos."
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
	echo "Please correct this later since dmsdos won't run without."
	WARN=y
fi

if [ "$VP" = "2.1" -a "$CONFIG_UMSDOS_FS" != "n" -a "$SUBLEVEL" -lt 94 ];
then
	echo
	echo "ERROR: you have enabled UMSDOS in $VPS kernel."
	echo "This configuration cannot work due to incompatibility."
	echo "Please upgrade your kernel to at least 2.1.94 or disable UMSDOS."
	echo "Please read the documentation for details."
	exit 1
fi

if [ "$VP" = "2.0" -a "$SUBLEVEL" -lt 29 ];
then
	echo
	echo "WARNING: your kernel version $VPS is horribly outdated."
	echo "This configuration has never been tested. If it fails please"
	echo "upgrade your kernel to 2.0.29 or newer."
	echo "Please read the documentation for details."
	WARN=y
fi

if [ "$VP" = "2.1" -a "$SUBLEVEL" -lt 76 ];
then
	echo
	echo "ERROR: your kernel version $VPS is too old for CVF-FAT."
	echo "This configuration is unsupported."
	echo "Please upgrade your kernel to 2.1.94 or newer or use 2.0.xx (xx>=29)."
	echo "Please read the documentation for details."
	exit 1
fi

if [ "$WARN" = "y" ];
then
	echo
	echo "Press ENTER to continue."
	read junk
else
	echo "kernel configuration is OK."
fi

exit 0
