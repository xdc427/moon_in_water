 sort -n -t: -k 4 | awk -F: '/moon_pipe:.*:pipe_test:/{ 
	if( $5 == "useing" ){ useing += $6; useing_num++;}
	else if( $5 == "pipe_closed" ){ closed++; if( useing != 0 ){ print "useing error"; exit; } }
	else if( $5 == "mutex_ref" ){ mutex_ref += $6; mutex_num++; }
	else if( $5 == "pipe_init_done" ){ init++ }
	else if( $5 == "del_mutex" ){ del_mutex++; if( mutex_ref != 0 && init != 1 ){ print "del mutex error"; exit } }
	else if( $5 == "pipe_get_ref" ){ get_ref_num++; if( init != 1 ){ print "get ref error"; exit } }
	 } 
	 END{ print "useing:"useing_num" pipe_closed:"closed" mutex_ref:"mutex_num" del_mutex:"del_mutex" get_ref:"get_ref_num }'
