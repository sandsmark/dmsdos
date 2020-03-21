#!/bin/sh

if [ ! -f /etc/magic ];
then
	echo "/etc/magic not found, skipped."
	exit 0
fi

grep "CVF formats" /etc/magic > /dev/null
if [ "$?" != "1" ];
then
	echo "/etc/magic contains already CVF magic numbers."
	exit 0
fi

echo "If you want to make the command file(1) recognize CVFs the magic numbers"
echo "can be added to the database /etc/magic automatically now."
echo -n "Do you want me to do this ? (Y/N) "
read ANS junk
if [ "$ANS" = y -o "$ANS" = Y ];
then
	cat patches/magic >> /etc/magic
fi

exit 0
