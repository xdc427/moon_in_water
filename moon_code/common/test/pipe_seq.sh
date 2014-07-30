awk -F: '{ if( $1 == "moon_pipe" && $3 == "pipe_test" ){
	if( idarray[ $5 ] == "" ){ idarray[ $5 ] = $4 } print idarray[ $5 ]":"$0; 
}}' $1|sort -t: -k1,1n -k6,6 -k5,5n | cut -d: -f2- |awk -F: '{
if( cur_id == "" || cur_id != $5 || pipe_free == 1 ){
if( cur_id != "" ){
print cur_id":pipe_new:"pipe_new":mutex_ref:"mutex_ref":pipe_init_done:"pipe_init_done":pipe_get_ref:"pipe_get_ref":del_mutex:"del_mutex":useing:"useing":pipe_ref:"pipe_ref":pipe_closed:"pipe_closed":pipe_free:"pipe_free;}
pipe_new=0;
mutex_ref=0;
pipe_init_done=0;
pipe_get_ref=0;
del_mutex=0;
useing=0;
pipe_ref=1;
pipe_closed=0;
pipe_free=0;
cur_id = $5;}
if( $6 == "pipe_new"){ pipe_new += $(6+1); if( !( 1 ) ){ print "pipe_new error"; exit 1 }}
else if( $6 == "mutex_ref"){ mutex_ref += $(6+1); if( !( 1 ) ){ print "mutex_ref error"; exit 1 }}
else if( $6 == "pipe_ref"){ pipe_ref += $(6+1); if( !( 1 ) ){ print "pipe_ref error"; exit 1 }}
else if( $6 == "useing"){ useing += $(6+1); if( !( 1 ) ){ print "useing error"; exit 1 }}
else if( $6 == "pipe_free"){ pipe_free += $(6+1); if( !( pipe_closed == 1 && pipe_ref == 0 ) ){ print "pipe_free error"; exit 1 }}
else if( $6 == "pipe_get_ref"){ pipe_get_ref += $(6+1); if( !( pipe_init_done == 1 ) ){ print "pipe_get_ref error"; exit 1 }}
else if( $6 == "pipe_init_done"){ pipe_init_done += $(6+1); if( !( pipe_new == 1 ) ){ print "pipe_init_done error"; exit 1 }}
else if( $6 == "pipe_closed"){ pipe_closed += $(6+1); if( !( useing == 0 ) ){ print "pipe_closed error"; exit 1 }}
else if( $6 == "del_mutex"){ del_mutex += $(6+1); if( !( mutex_ref == 0 && pipe_init_done == 1 ) ){ print "del_mutex error"; exit 1 }}
else {} }
END{
print cur_id":pipe_new:"pipe_new":mutex_ref:"mutex_ref":pipe_init_done:"pipe_init_done":pipe_get_ref:"pipe_get_ref":del_mutex:"del_mutex":useing:"useing":pipe_ref:"pipe_ref":pipe_closed:"pipe_closed":pipe_free:"pipe_free;}'
