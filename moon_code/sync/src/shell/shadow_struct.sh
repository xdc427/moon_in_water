
#8种基本类型
basetype=( '\\*' 'void *' '\\bchar\\b' 'char' '\\bshort\\b' 'short' 
'\\blong\\b.*\\blong\\b' 'long long' '\\bint\\b' 'int' '\\blong\\b.*\\bdouble\\b'  
'long double' '\\bdouble\\b' 'double' '\\bfloat\\b' 'float' )

name="$1"
read all_num
read var_num
base_num="$((${#basetype[@]}/2))"
all_num="$(($all_num+$base_num))"
echo "#include\"shadow_struct.h\""
echo "#include\"shadow_base.h\""
echo "#include\"structs_$name.h\""
echo "#include<stdio.h>"
echo ""
echo "static char buffer_$name[ sizeof( struct_all_s ) + ( sizeof( struct_one ) + sizeof( struct_one_s ) ) * $all_num + sizeof( var_s ) * $var_num ];"
echo ""
echo "struct_all shadow_struct_""$name""_init()"
echo "{"
echo "	struct_all struct_head;"
echo "	struct_one * one_arr;"
echo "	struct_one tmp_one;"
echo "	var tmp_var;"
echo "	int i;"
echo ""
echo "	struct_head = ( struct_all )buffer_$name;"
echo "	struct_head->num = $all_num;"
echo "	one_arr = ( struct_one * )(	struct_head + 1 );"
echo "	tmp_one = ( struct_one )( one_arr + $all_num );"
for ((i=0;i<base_num;i++))
do
	echo "	snprintf( tmp_one->name , sizeof( tmp_one->name ), "'"'${basetype[$(($i*2))]}'"'" );"
	echo "	tmp_one->size = sizeof( ${basetype[$(($i*2+1))]} );"
	echo "	tmp_one->num = 0;"
	echo "	*one_arr++ = tmp_one;"
	echo "	tmp_one++;"
done
echo "	tmp_var = ( var )( tmp_one ) - 1;"
awk -F: 'BEGIN{cur_num=0;}
{
	if(cur_num==0){
		cur_num=$5-1;
		struct_name=$2
		print "\ttmp_one = ( struct_one )( tmp_var + 1 );"
		print "\tsnprintf( tmp_one->name , sizeof( tmp_one->name ), \"\\\\b"$2"\\\\b\" );";
		print "\ttmp_one->size = sizeof( struct "$2" );";
		print "\ttmp_one->num = "cur_num";";
		print "\t*one_arr++ = tmp_one;"
		print "\ttmp_var = ( var )( tmp_one + 1 ) - 1;";
	}else{
		print "\ttmp_var++;";
		print "\tsnprintf( tmp_var->name, sizeof( tmp_var->name ), \""$2"\" );";
		print "\tsnprintf( tmp_var->type, sizeof( tmp_var->type ), \""$1"\" );";
		print "\ttmp_var->size = sizeof( ( ( struct "struct_name" * )0 )->"$2" );";
		print "\ttmp_var->addr = &( ( ( struct "struct_name" * )0 )->"$2" );";
		print "\ttmp_var->array_d = "$3";";
		print "\ttmp_var->array_elem_size = sizeof( "$1" );";
		print "\ttmp_var->is_hide = "$4";";
		cur_num--;
	}
}'
echo "	complete_var_type( struct_head );"
echo "	return struct_head;"
echo "}"

