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
			"$dir/moonpp.sh" "$dir/structtxt_$name" "$dir/vartxt_$name" "$dir/structs_$name.h" "$dir/headstxt_$name" <"$1" 
		fi
	fi
}

dir=$( dirname "$0" )
rm -f "$dir/version_init.c"
rm -f "$dir/shadow_struct.h"

for user_dir in $@
do
	name=${user_dir##*/}
	rm -f "$dir/structtxt_$name"
	rm -f "$dir/vartxt_$name"
	rm -f "$dir/structs_$name.h"
	rm -f "$dir/headstxt_$name"
	rm -f "$dir/shadow_struct_$name.c"
	explor_dir "$user_dir" 
	if [ -s "$dir/structtxt_$name" ] && [ -s "$dir/vartxt_$name" ]
	then
		"$dir/moonpp_next.sh" "$dir/structtxt_$name" | "$dir/shadow_struct.sh" "$name" >>"$dir/shadow_struct_$name.c"
		"$dir/shadow_var_func.sh" "$name" "$dir/shadow_struct_$name.c" "$dir/shadow_struct.h" <"$dir/vartxt_$name"
		"$dir/shadow_var.sh" "$dir/vartxt_$name" "$name" >>"$dir/version_init.c"
	fi
done

cat "$dir/_shadow_struct.h" >>"$dir/shadow_struct.h"
echo "void version_init()" >>"$dir/version_init.c"
echo "{" >>"$dir/version_init.c"
for user_dir in $@
do
	name=${user_dir##*/}
	echo "struct_all shadow_struct_""$name""_init();" >>"$dir/shadow_struct.h"
	echo "	version_""$name""_init();" >>"$dir/version_init.c"
done
echo "}" >>"$dir/version_init.c"
