# $1:输入文件或目录
# $2:输出目录

function explor_dir()
{
	if [ -d "$1" ]
	then
		for file in "$1"/*
		do
			explor_dir "$file"
		done
	elif [ -f "$1" ]
	then
		declare -l suffix=${1##*.}
		if [ "$suffix" = "c" ] || [ "$suffix" = "h" ]
		then
			echo "$1"
			./moonpp.sh shadow_struct.h <"$1" >>tmptxt
		fi
	fi
}

rm -f tmptxt
rm -f shadow_struct.h
rm -f shadow_struct.c

explor_dir "$1"
if [ -s "./tmptxt" ]
then
	./moonpp_next.sh tmptxt | ./shadow_struct.sh shadow_struct.h >shadow_struct.c
	if [ -d "$2" ]
	then
		mv -f shadow_struct.h "$2"
		mv -f shadow_struct.c "$2"
		mv -f shadow_base.h   "$2"
#		rm -f tmptxt
	fi
fi
