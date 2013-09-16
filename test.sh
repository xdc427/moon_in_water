i=0
while ((i<$1))
do
./test1 >read.txt
echo "compare start"
diff <( awk -F: '{ if($2=="ring_read")printf("%s",$5)}' read.txt) <( awk -F: '{ if($2=="ring_write")printf("%s",$5) }' read.txt) 1>/dev/null
if [ "$?" = "0" ]
then
	echo "ok"
else
	echo "fail"
	break
fi

i=$(($i+1))
done
