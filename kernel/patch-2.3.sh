
#check for version number
if [ "$VP" = "2.3" -a "$SUBLEVEL" -lt 99 ];
then
	echo "At least kernel 2.3.99 from the 2.3 series is required."
	exit 1
fi

#check cvf-fat interface

ID=`grep CVF-FAT-VERSION-ID /usr/src/linux/fs/fat/cvf.c | cut -f2 -d: | tr -d " "`
echo "Found CVF-FAT interface version $ID"

ID1=`echo $ID|cut -f1 -d.`
ID2=`echo $ID|cut -f2 -d.`
ID3=`echo $ID|cut -f3 -d.`

if [ "$ID1" -lt 2 ];
then
	echo "I'm going to update the CVF-FAT interface in your kernel."
	echo "Press Enter to continue or CTRL-C to abort."
	read junk
	( cd /usr/src/linux; patch -p1 ) < patches/cvf-fat-2.0.diff >& /tmp/cvf.diff.log
fi

echo "This looks like no patches are needed :)"
