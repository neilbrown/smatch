#!/bin/bash

bin_dir=$(dirname $0)
remove=$(echo ${bin_dir}/../smatch_data/kernel.paholes.remove)
tmp=$(mktemp /tmp/smatch.XXXX)

for i in $(find -name \*.ko) ; do
    pahole -E $i 2> /dev/null
done | $bin_dir/find_expanded_holes.pl | sort -u > $tmp

pahole -E vmlinux | $bin_dir/find_expanded_holes.pl | sort -u >> $tmp

sort -u $tmp > kernel.paholes
mv kernel.paholes $tmp

echo "// list of functions and the argument they free." > kernel.paholes
echo '// generated by `gen_paholes.sh`' >> kernel.paholes

cat $tmp $remove $remove 2> /dev/null | sort | uniq -u >> kernel.paholes

rm $tmp
echo "Generated kernel.paholes"