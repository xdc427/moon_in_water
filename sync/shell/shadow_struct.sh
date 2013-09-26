
echo "#include<stdio.h>"
echo "#include\"shadow_base.h\""
echo "#include\"$1\""

read all_num
read var_num
echo ""
echo "static char buffer[ sizeof( struct_all_s ) + sizeof( struct_one_s ) * $all_num + sizeof( var_s ) * $var_num ];"
echo ""
echo "void shadow_struct_init()"
echo "{"
echo "	struct_all struct_head;"
echo "	struct_one tmp_one;"
echo "	var tmp_var;"
echo "	int i;"
echo ""
echo "	struct_head = ( struct_all )buffer;"
echo "	struct_head->num = $all_num;"
echo "	tmp_var = ( var )( struct_head + 1 ) - 1;"
awk -F: 'BEGIN{cur_num=0;}
{
	if(cur_num==0){
		cur_num=$4-1;
		struct_name=$2
		print "\ttmp_one = ( struct_one )( tmp_var + 1 );"
		print "\tsnprintf( tmp_one->name , sizeof( tmp_one->name ), \""$2"\" );";
		print "\ttmp_one->size = sizeof( struct "$2" );";
		print "\ttmp_one->num = "cur_num";";
		print "\ttmp_var = ( var )( tmp_one + 1 ) - 1;";
	}else{
		print "\ttmp_var++;";
		print "\tsnprintf( tmp_var->name, sizeof( tmp_var->name ), \""$2"\" );";
		print "\tsnprintf( tmp_var->type, sizeof( tmp_var->type ), \""$1"\" );";
		print "\ttmp_var->size = sizeof( ( ( struct "struct_name" * )0 )->"$2" );";
		print "\ttmp_var->size = &( ( ( struct "struct_name" * )0 )->"$2" );";
		print "\ttmp_var->array_d = "$3";";
		cur_num--;
	}
}'
echo "	return;"
echo "}"

