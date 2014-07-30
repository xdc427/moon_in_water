#!/bin/bash
#配置文件：
#第一行:   过滤条件
#第二行:   id字段号:同id分隔代码
#节点:字段号:初始值:测试代码
#c文件中：
#节点:节点累加值

awk -F: '
NR==1{
	filter = $0;
}
NR==2{
	if( NR != 2 ){ print "line 2 error"; exit 1 }
	id = $1;
	id_code = $2;
	printf( "awk -F: '"'"'{ if( %s ){\n", filter );
 	printf( "	if( idarray[ $%d ] == \"\" ){ idarray[ $%d ] = $4 } print idarray[ $%d ]\":\"$0; \n}}'"' \$1"'", id, id, id );   
	printf( "|sort -t: -k1,1n -k%d,%d -k5,5n | cut -d: -f2- |", id+1, id+1 );
}
NR>2{
	if( NF != 4 ){ print "line "NR" error"; exit 1 }
	nodes_index[ NR-2 ] = $1;
	nodes[ $1 ] = $2;
	nodes_test[ $1 ] = $4;
	nodes_init[ $1 ] = $3;
}
END{
	print "awk -F: '"'"'{"
	print "if( cur_id == \"\" || cur_id != $"id" || "id_code" ){"
	print "if( cur_id != \"\" ){"
	printf( "print cur_id" );
	for( i = 1; i <= length( nodes_index ); i++ ){
		printf( "\":%s:\"%s", nodes_index[ i ], nodes_index[ i] );
	}
	print ";}";
	for( i = 1; i <= length( nodes_index ); i++ ){
		print nodes_index[ i ]"="nodes_init[ nodes_index[ i ] ]";";
	}
	print "cur_id = $"id";}"
	for( i in nodes ){
		printf( "if( $%d == \"%s\"){ %s += $(%d+1); if( !( %s ) ){ print \"%s error\"; exit 1 }}\nelse ", nodes[i], i, i, nodes[i], nodes_test[i], i );
	}
	print "{} }"
	print "END{"
	printf( "print cur_id" );
	for( i = 1; i <= length( nodes_index ); i++ ){
		printf( "\":%s:\"%s", nodes_index[ i ], nodes_index[ i] );
	}
	printf( ";" );
	print "}'"'"'"
}' $1

