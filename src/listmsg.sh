#!/bin/bash

# This script tries to find all kernel messages in the dmsdos sources.
# It's not 100% and may print some garbage, but better than nothing.

# Usage: 
#	LIST_MESSAGES                list all (incl. debug) messages
#	LIST_MESSAGES pattern        list only messages with pattern in it
#	LIST_MESSAGES -LOG           list normal messages (without debug messages)

if [ "$1" != "" ];
then
	if [ "$1" != "-LOG" ];
	then
	( cat dblspace*.c *.c-2.0.33 *.c-2.1.80 dstacker*.c | awk -f remove_comments.awk | grep DMSDOS | grep $1 | cut -f2 -d\" -s | cut -f1 -d\\ | sort -b -d -f | uniq ) 2>/dev/null
	else
	( cat dblspace*.c *.c-2.0.33 *.c-2.1.80 dstacker*.c | awk -f remove_comments.awk | grep DMSDOS | grep -v LOG | cut -f2 -d\" -s | cut -f1 -d\\ | sort -b -d -f | uniq ) 2>/dev/null
	fi
else
( cat dblspace*.c *.c-2.0.33 *.c-2.1.80 dstacker*.c | awk -f remove_comments.awk | grep DMSDOS | cut -f2 -d\" -s | cut -f1 -d\\ | sort -b -d -f | uniq ) 2>/dev/null
fi
