#!/bin/bash
cat - > $1
rm -fr $1.?*
awk '{b=($1=="#Bookmark")}(b && $0 ~ /BEGIN/){a=$NF" "a}{split(a,ar," ");for(i in ar)print $0 > "'$1'."ar[i]}(b && $0 ~ /END/){gsub($NF" ","",a)}' $1
