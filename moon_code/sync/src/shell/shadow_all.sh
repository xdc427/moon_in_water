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
			"$dir/moonpp.sh" "$target_dir/structtxt_$name" "$target_dir/vartxt_$name" "$target_dir/structs_$name.h" "$target_dir/headstxt_$name" <"$1" 
		fi
	fi
}

dir=$( dirname "$0" )
if [ "$#" -lt "2" ]
then
	exit 1
fi
target="$1"
target_dir="$dir/$target/"
all_opts=($@)
dirs=(${all_opts[@]:1})
echo "${dirs[@]}"

rm -rf "$target_dir"
mkdir "$target_dir"

echo "#include\"$target/shadow_struct_$target.h\"" >> "$target_dir/version_init_$target.c"
for user_dir in ${dirs[@]}
do
	name=${user_dir##*/}
	explor_dir "$user_dir" 
	if [ -s "$target_dir/structtxt_$name" ] && [ -s "$target_dir/vartxt_$name" ]
	then
		"$dir/moonpp_next.sh" "$target_dir/structtxt_$name" | "$dir/shadow_struct.sh" "$name" >>"$target_dir/shadow_struct_$name.c"
		"$dir/shadow_var_func.sh" "$name" "$target_dir/shadow_struct_$name.c" "$target_dir/shadow_struct_$target.h" <"$target_dir/vartxt_$name"
		"$dir/shadow_var.sh" "$target_dir/vartxt_$name" "$name" >>"$target_dir/version_init_$target.c"
	fi
done

#cat "$dir/_shadow_struct.h" >>"$dir/shadow_struct_$target.h"
echo "void version_init()" >>"$target_dir/version_init_$target.c"
echo "{" >>"$target_dir/version_init_$target.c"
echo "	shadow_env_init();" >>"$target_dir/version_init_$target.c"
for user_dir in ${dirs[@]}
do
	name=${user_dir##*/}
	echo "struct_all shadow_struct_""$name""_init();" >>"$target_dir/shadow_struct_$target.h"
	echo "	version_""$name""_init();" >>"$target_dir/version_init_$target.c"
done
echo "}" >>"$target_dir/version_init_$target.c"

