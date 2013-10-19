input="$1"

head_arr=($(grep -n "^head$" $input| cut -d: -f1))
tail_arr=($(grep -n "^tail$" $input| cut -d: -f1))
var_all=$(wc -l $input | cut -d" " -f1)
var_all=$(($var_all-3*${#head_arr[@]}))

echo "${#head_arr[@]}"
echo "$var_all"
i=0
{
while read  line
do
	if [ "$line" = "head" ]
	then
		num="$((${tail_arr[$i]}-${head_arr[$i]}-1))"
		read line
		echo "$line:$num"
		i=$(($i+1))
	elif [ "$line" != "tail" ]
	then
		echo "$line"
	fi
done
}<"$input"
