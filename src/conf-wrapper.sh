#/bin/sh

echo
echo "dmsdos seems not configured. I'm going to call dmsdos configuration now."
echo "If you do not want me to do this, press CTRL-C. Press Enter to continue."
read junk

CT=config
if [ -f /usr/src/linux/scripts/lxdialog/lxdialog ];
then
CT=menuconfig
fi

make $CT
