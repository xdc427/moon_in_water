#ifndef _MOON_PACKET_H_
#define _MOON_PACKET_H_

#define PACKET_MODEL "moon_packet"

typedef struct packet_elem_s{
	char id[ 64 ];
	int len;
	void * p_data;
	void ( * data_free )( void * );
	int ( * process_func[ 2 ] )( char *, int );//0 unpack func, 1 pack func
} packet_elem_s;
typedef packet_elem_s * packet_elem;

typedef struct packet_model_s packet_model_s;
typedef packet_model_s * packet_model;

int packet_model_add( char * name, packet_elem p_elem, int is_head );
packet_model get_pack_instantiation( char * name );
int create_packet_buf( packet_model p_model );
packet_model get_unpack_instantiation( char * name, char * buf, int len );
int set_packet_elem_len( packet_model p_model, char * id, int len );
int set_packet_elem_len_position( packet_model p_model, int len, int position );
int get_packet_elem_len( packet_model p_model );
int set_packet_elem_data_position( packet_model p_model, void * data, int position );
void * get_packet_elem_data_position( packet_model p_model, int position );
char * get_packet_elem_buf( packet_model p_model );
int next_packet_elem( packet_model p_model );
void free_packet( packet_model p_model );
void free_packet_without_buf( packet_model p_model );
int process_packet( packet_model p_model );
int get_packed_len( packet_model p_model ); //获取当前已打包的数据长度
void packet_model_print( );
void packet_instantiation_model_print( packet_model p_model );

#endif
