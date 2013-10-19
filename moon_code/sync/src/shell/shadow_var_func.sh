awk -v name="$1" -v func_file="$2" -v include_file="$3" -F: '{
	
	print "inline void * get_"name"_"$2"_addr()\n\
{\n\
	return &"$2";\n\
}\n\
\n\
inline unsigned long get_"name"_"$2"_size()\n\
{\n\
	return sizeof( "$2" );\n\
}\n\
\n\
inline unsigned long get_"name"_"$2"_elem_size()\n\
{\n\
	return sizeof( "$1" );\n\
}\n\
\n\
inline unsigned long get_"name"_"$2"_len()\n\
{\n\
	return sizeof( "$2" ) / sizeof( "$1" );\n\
}\n\
\n\
" >> func_file

	print "inline void * get_"name"_"$2"_addr();\n\
inline unsigned long get_"name"_"$2"_size();\n\
inline unsigned long get_"name"_"$2"_elem_size();\n\
inline unsigned long get_"name"_"$2"_len();" >> include_file

}'
