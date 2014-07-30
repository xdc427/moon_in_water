awk -F: '{ if( ( $1 == "common_socket" && $3 == "socket_seq" ) || ( $1 == "moon_common" && $3 == "common_useing" ) ){
	if( idarray[ $5 ] == "" && $6 == "new"){ idarray[ $5 ] = $4 } if( idarray[ $5 ] != "" )print idarray[ $5 ]":"$0; 
}}' "$1" |sort -t: -k1,1n -k6,6 -k5,5n | cut -d: -f2- |awk -F: '{
if( cur_id == "" || cur_id != $5 || free == 1 ){
if( cur_id != "" ){
print cur_id":useing:"useing":closed:"closed":free:"free;}
useing=0;
closed=0;
free=0;
cur_id = $5;}
if( $6 == "useing"){ useing += $(6+1); if( !( 1 ) ){ print "useing error"; exit 1 }}
else if( $6 == "free"){ free += $(6+1); if( !( closed == 1 ) ){ print "free error"; exit 1 }}
else if( $6 == "closed"){ closed += $(6+1); if( !( useing == 0 ) ){ print "closed error"; exit 1 }}
else {} }
END{
print cur_id":useing:"useing":closed:"closed":free:"free;}'
