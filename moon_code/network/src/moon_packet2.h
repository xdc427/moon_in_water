#ifndef _MOON_PACKET2_H_
#define _MOON_PACKET2_H_

typedef struct{
	char * buf;//packet_buf + 1
	int offset;
	int len;
} buf_desc_s;
typedef buf_desc_s * buf_desc;

struct buf_head_s;
typedef struct buf_head_s buf_head_s;
typedef buf_head_s * buf_head;

void * packet_buf_malloc( int size );
void packet_buf_free( void * buf );
int add_buf_to_packet( buf_head * pp_head, buf_desc p_desc );
int get_packet_len( buf_head p_head );
void free_total_packet( buf_head p_head );
void begin_travel_packet( buf_head p_head );
int get_next_buf( buf_head p_head, char ** buf, int * p_len );
void begin_split_packet( buf_head p_head, int len );
int get_next_packet( buf_head p_head, int len, buf_head * pp_head );
buf_head dump_packet( buf_head p_head );

#endif

