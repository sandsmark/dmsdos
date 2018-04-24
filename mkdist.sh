#!/bin/sh

DIRNAME=`pwd`
DIRNAME=`basename $DIRNAME`

ARCHIVENAME=$DIRNAME.tgz

cd ..

echo "I'm going to create or overwrite file $ARCHIVENAME in directory"
pwd
echo "Press Enter to continue or CTRL-C to abort."
read junk

rm -f $ARCHIVENAME
tar cvzf $ARCHIVENAME $DIRNAME
