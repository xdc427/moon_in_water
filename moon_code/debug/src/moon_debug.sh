sed -e 's/^[ 	]*//g' -e 's/,//g' $1 | awk '
BEGIN{
	skip=0;
	start=0;
	print "#ifndef _MOON_DEBUG_H_"
	print "#define _MOON_DEBUG_H_"
	print ""
	print "#define MODENAME \"\""
	print ""
}

/\/\/group/{
	if(num>0){
		for(i=num-1;i>=0;i--){
			print "#ifdef LEVEL_"save[i]; 
			print "#define MOON_PRINT_"save[i]"( level, xid, body, ... ) moon_print( MODENAME, level, xid, body ,##__VA_ARGS__ )"; 
			print "#define MOON_"save[i]"( code )  code"
			for(j=i-1;j>=0;j--){ 
				print "#define LEVEL_"save[j]
			}
			print "#else"; 
			print "#define MOON_PRINT_"save[i]"( level, xid, body, ... ) "; 
			print "#define MOON_"save[i]"( code ) "
			print "#endif";
			print "";
		}
	}
	num=0;
	skip=1
} 
{if(skip==1){
	skip=0;
	start=1;
}else if(start)
	save[num++]=$1
}

END{ 
	print "#define MOON_PRINT( level, xid, body, ... ) MOON_PRINT_##level( #level, xid, body ,##__VA_ARGS__ )";
	print "";
}' 
cat $1 
echo 
echo "void moon_print( char * name, char * level, char * xid, char * body, ... );"
echo 
echo "#endif"

