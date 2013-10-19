echo "a"
echo "1000"
for (( i=10;i<1010;i++))
do
	echo -n "$i "
done
echo ""
while read line
do
	echo "$line"
done
echo "q"
