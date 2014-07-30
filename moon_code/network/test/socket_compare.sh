#!/bin/bash
file=$1
tmpfile="tmp_$file"
awk -F: '{if( $1=="common_socket_test" && ( $6=="send" || $6=="recv" ) )print $0;}' $file | sort -t: -k5,5n > $tmpfile
sockets=($(awk -F: '{sarray[$5]=NR;}END{for(i in sarray)print i" "sarray[i] | "sort -k1,1n"}' $tmpfile ))
#awk -F: '{sarray[$5]++;}END{for( i in sarray ){print i":"sarray[i]}}' $tmpfile 

#echo "${sockets[@]}"
echo "sockets_num:${#sockets[@]}"
#exit
ok=0
fail=0
i=0
startn=1
while (($i<${#sockets[@]}))
do
j=$(($i+1))
endn=${sockets[$j]}
sock=${sockets[$i]}
num=$(($endn-$startn+1))
recvfile="tmp_recv_$sock.txt"
sendfile="tmp_send_$sock.txt"
head -$num | awk -F: -v recvfile=$recvfile -v sendfile=$sendfile '{if($6=="recv"){printf("%s",$7) > recvfile }else{ printf("%s",$7) > sendfile }}'
#./compare <( awk -F: -v sn=$startn -v en=$endn 'NR==sn,NR==en{if($6=="recv")printf("%s",$7)}' $tmpfile ) <( awk -F: -v sn=$startn -v en=$endn 'NR==sn,NR==en{if($6=="send")printf("%s",$7)}' $tmpfile )
./compare "$recvfile" "$sendfile" 1>/dev/null
if [ "$?" = "0" ]
then
#	echo  "ok:$sock"
	ok=$(($ok+1))
else
	echo "$sock:fail"
	fail=$(($fail+1))
fi
if [ -f "$recvfile" ]
then
rm "$recvfile"
fi

if [ -f "$sendfile" ]
then
rm "$sendfile"
fi
i=$(($i+2))
startn=$(($endn+1))
done <$tmpfile
echo "ok:$ok:fail:$fail"
rm $tmpfile

