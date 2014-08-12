awk -F: '{ if( ( $1 == "moon_dns" && ( $6 == "domain_mem" || $6 == "memalloc" ) ) ){
	if( idarray[ $5 ] == "" ){ idarray[ $5 ] = $4 } print idarray[ $5 ]":"$0; 
}}' $1|sort -t: -k1,1n -k6,6 -k5,5n | cut -d: -f2- |awk -F: '{
if( cur_id == "" || cur_id != $5 || 0 ){
if( cur_id != "" ){
print cur_id":domain_mem:"domain_mem":memalloc:"memalloc;}
domain_mem=0;
memalloc=0;
cur_id = $5;}
if( $6 == "memalloc"){ memalloc += $(6+1); if( !( 1 ) ){ print "memalloc error"; exit 1 }}
else if( $6 == "domain_mem"){ domain_mem += $(6+1); if( !( 1 ) ){ print "domain_mem error"; exit 1 }}
else {} }
END{
print cur_id":domain_mem:"domain_mem":memalloc:"memalloc;}'
