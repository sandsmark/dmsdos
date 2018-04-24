#
# This is an awk script which removes comments in c files.
# it does not follow #include directives.
#

BEGIN{
	incomment=0
}

# eliminate comments
{
    # remove all comments fully contained on a single line
	gsub("\\/\\*.*\\*\\/", "")
	if (incomment) {
		if ($0 ~ /\*\//) {
			incomment = 0;
			gsub(".*\\*\\/", "")
		} else {
			next
		}
	} else {
		# start of multi-line comment
		if ($0 ~ /\/\*/)
		{
			incomment = 1;
			sub("\\/\\*.*", "")
		} else if ($0 ~ /\*\//) {
			incomment = 0;
			sub(".*\\*\\/", "")
		}
	}
	print $0
}

END{
}
