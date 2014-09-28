#!/bin/bash
cmds=(
'{ "parse":"google.com" }'
'{ "parse":"google.com", "flags":1 }'
'{ "parse":"google.com", "flags":2 }'
'{ "parse":"baidu.com" }'
'{ "parse":"baidu.com", "flags":1 }'
'{ "parse":"baidu.com", "flags":2 }'
'{ "parse":"facebooks.com" }'
'{ "parse":"facebooks.com", "flags":1 }'
'{ "parse":"facebooks.com", "flags":2 }'
'{ "usleep":50000 }'
'{ "usleep":1000 }'
)

i=0
echo "["
while (($i<10000))
do
	n=$(( $RANDOM % 100 ))
	if [ "$n" -lt "5" ]
	then
		m=$(( $RANDOM % ( i + 1 ) ))
		echo '{ "cancel":'$m' },'
	else
		m=$(( n % ${#cmds[@]} ))
		echo "${cmds[$m]},"
	fi
	i=$(($i+1))
done
echo "]"

