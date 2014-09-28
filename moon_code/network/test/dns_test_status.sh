#!/bin/bash
while read line
do
	grep "$line" $1 > /dev/null
	if [ "$?" != "0" ]
	then
		echo "don't find: $line"
	fi
done < ./dns_test_status.txt

