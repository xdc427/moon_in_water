#通过SHADOW_STRUCT关键字来分析结构体

a=("short" "long" "unsigned" "signed")
b=("int" "char" "float" "double" "void")
c=("struct" "enum" "union")
outfile=$1

function start_analyse()
{
	local left="$@"
	local status="head"
	local index line index_end
	local tmp

	while [ "$status" != "tail" ]
	do
		if [ "$status" = "head" ]
		then
#			echo "$left"
			index="$( expr index "$left" "{" )"
#			echo "$index"
			if [ "$index" != "0" ]
			then
				echo "head"
				tmp="${left:0:$index}"	
				tmp=$(trim "$tmp")
				echo "$tmp" >>"$outfile"
				struct_body "${left:0:$(($index-1))}"
				left="${left:$index}"
				status="body"
				continue
			fi
		else
#			echo "$left"
			index="$( expr index "$left" ";" )"
			index_end="$( expr index "$left" "}" )"
#			echo "$index"
			if ((index_end!=0 && (index==0 || index_end<index)))
			then
				echo "tail"
				status="tail"
				echo "};">>"$outfile"
				echo "">>"$outfile"
				continue
			fi
			if [ "$index" != "0" ]
			then
				tmp="${left:0:$index}"
				tmp=$(trim "$tmp")
				if [ "$tmp" != ";" ]
				then
					echo "	$tmp" >>"$outfile"
					struct_body "${left:0:$(($index-1))}"
				fi
				left="${left:$index}"
				continue
			fi
		fi
		read line 
		if [ "$?" != "0" ]
		then
			echo "read error:in process struct"
			exit
		fi
		left="$left $line"
#		echo "$left"
	done
#	echo "end======"
}

function struct_body()
{
	local arr="$@"
	local tmp index append name

	index=$( expr index "$arr" "[")
	if [ "$index" != "0" ]
	then
		tmp="${arr//[^[]}"
		append="${tmp//[[]/*}"
		arr="${arr:0:$(($index-1))}"
	fi
	arr=$(trim "$arr")
	name="${arr##*[ 	\*]}"
	tmp="${arr%$name}"
	tmp="$(trim "$tmp")"
	echo "$tmp:$name:${#append}"
}
function trim()
{
	local tmp="$@"

	echo  -n "$( echo "$tmp"|sed -e 's/^[ 	]*//' -e 's/[ 	]*$//' )"
}
while read line 
do
#	echo "$line"
	begin=$( expr match "$line" "^SHADOW_STRUCT" )
#	echo "$begin"
	if [ "$begin" != "0" ]
	then
		left="${line#SHADOW_STRUCT}"
#		echo "$left"
		start_analyse "$left"
		continue
	fi  
	begin=$( expr match "$line" "^SHADOW_VAR" )
	if [ "$begin" != "0" ]
	then
		left="${line#SHADOW_VAR}"
		left="${left%%;*}"
		left="${left%%=*}"
		struct_body "$left" >&2
	fi
done

