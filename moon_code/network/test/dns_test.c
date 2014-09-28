#include<stdio.h>
#include<netdb.h>
#include<string.h>
#include<stdlib.h>
#include<netinet/in.h>
#include<arpa/inet.h>
#include<sys/time.h>
#include<unistd.h>
#include<json-c/json.h>
#include"moon_dns.h"
#include"moon_pipe.h"
#include"common_interfaces.h"

typedef struct test_private_s{
	char domain[ 256 ];
	int status;// 0 undo ,> 0 done
} test_private_s;
typedef test_private_s * test_private;

static void query_fail( void * p_data, void * p_pipe );
static void query_success( void * p_data, void * p_pipe, void * p_addrs );

STATIC_BEGAIN_INTERFACE( dns_test_hub )
STATIC_DECLARE_INTERFACE( dns_listener_interface_s )
STATIC_DECLARE_INTERFACE( pipe_listener_interface_s )
STATIC_END_DECLARE_INTERFACE( dns_test_hub, 2 )
STATIC_GET_INTERFACE( dns_test_hub, dns_listener_interface_s, 0 ) = {
	.on_getaddrinfo = query_success
}
STATIC_GET_INTERFACE( dns_test_hub, pipe_listener_interface_s, 1 ) = {
	.close = query_fail
}
STATIC_END_INTERFACE( NULL )

#define QUERY_MAX_NUM 10000
static test_private private_table[ QUERY_MAX_NUM ];

static void query_success( void * p_data, void * p_pipe, void * p_addrs )
{
	test_private p_test;
	dns_addrs_interface p_addrs_i;
	int tmp;
	addr_elem p_elem;
	char ip_buf[ INET6_ADDRSTRLEN ];

	p_test = p_pipe;
	tmp = __sync_fetch_and_add( &p_test->status, 1 );
	if( tmp == 0 ){
		p_addrs_i = FIND_INTERFACE( p_addrs, dns_addrs_interface_s );
		p_elem = p_addrs_i->get_next( p_addrs, NULL );
		tmp = p_addrs_i->get_num( p_addrs );
		printf( "domain ok %d: %s\n", tmp, p_test->domain );
		while( p_elem != NULL ){
			inet_ntop( p_elem->family, p_elem->addr_buf, ip_buf, sizeof( ip_buf ) );
			printf( "%s\n",  ip_buf );
			p_elem = p_addrs_i->get_next( p_addrs, p_elem );
		}
		CALL_INTERFACE_FUNC( p_test, pipe_interface_s, close );
//		CALL_INTERFACE_FUNC( p_test, gc_interface_s, ref_dec );
	}
}

static int query_cancel( test_private p_test )
{
	int tmp;
	
	tmp = __sync_fetch_and_add( &p_test->status, 1 );
	if( tmp == 0 ){
		printf( "domain cancel: %s\n", p_test->domain );
		CALL_INTERFACE_FUNC( p_test, pipe_interface_s, close );
//		CALL_INTERFACE_FUNC( p_test, gc_interface_s, ref_dec );
		return 0;
	}
	return -1;
}

static void query_fail( void * p_data, void * p_pipe )
{
	test_private p_test;
	
	p_test = p_pipe;
	if( query_cancel( p_test ) >= 0 ){
		printf( "domain fail: %s\n", p_test->domain );
	}
}

int main( int argc, char * argv[] )
{
	void * p_dns;
	url_desc_s desc;
	dns_interface p_dns_i;
	int ret, len, sleep_us, i, flags, j, n;
	char c, buf[ 1024 + 1 ];
	char * url;
	void * p_pipe[ 2 ];
	test_private p_test;
	json_object * p_dns_obj, * p_obj, *p_tmp;
	FILE * fp;
	enum json_tokener_error jerr;
	json_tokener * tok;

	if( argc < 2 ){
		return 1;
	}
	p_dns = get_dnsinstance();
	p_dns_i = FIND_INTERFACE( p_dns, dns_interface_s );

	fp = fopen( argv[ 1 ], "r" );
	tok = json_tokener_new();
	jerr = json_tokener_continue;
	do{
		len = fread( buf, 1, sizeof( buf ) - 1, fp );
		if( len > 0 ){
			buf[ len ] = '\0';
			p_dns_obj = json_tokener_parse_ex( tok, buf, len );
		}else{
			break;
		}
	}while ( ( jerr = json_tokener_get_error( tok ) ) == json_tokener_continue );
	if (jerr != json_tokener_success)
	{
		fprintf(stderr, "Error: %s\n", json_tokener_error_desc(jerr));
		return 1;
	}
	json_tokener_free( tok );
	for( j = i = 0; i < json_object_array_length( p_dns_obj ); i++ ){
		p_obj = json_object_array_get_idx( p_dns_obj, i );
		if( json_object_object_get_ex( p_obj, "parse", &p_tmp ) == TRUE 
			&& j < QUERY_MAX_NUM ){
			url = ( char * )json_object_get_string( p_tmp );
			flags = 0;
			if( json_object_object_get_ex( p_obj, "flags", &p_tmp ) == TRUE ){
				flags = json_object_get_int( p_tmp );
			}
			ret = p_dns_i->analyse_url( p_dns, url, &desc );
			if( ret == 0 ){
				if( desc.scheame_match.rm_eo > desc.scheame_match.rm_so ){
					CUT_STRING( url, desc.scheame_match.rm_eo, c );
					printf( "scheame:%s\n", url + desc.scheame_match.rm_so );
					RECOVER_STRING( url, desc.scheame_match.rm_eo, c );
				}else{
					printf( "no scheame\n");
				}
				CUT_STRING( url, desc.domain_match.rm_eo, c );
				printf( "domain:%s\n", url + desc.domain_match.rm_so );
				len = 0;
				CALL_INTERFACE_FUNC( p_dns, pipe_listener_interface_s, get_pipe_data_len, &len );
				ret = pipe_new( p_pipe, sizeof( test_private_s ), len, 0 );
				if( ret >= 0 ){
					p_test = p_pipe[ 0 ];
					private_table[ j ] = p_test;
					snprintf( p_test->domain, sizeof( p_test->domain )
							, "%s", url + desc.domain_match.rm_so );
					CALL_INTERFACE_FUNC( p_pipe[ 0 ], pipe_interface_s, set_point_ref, &dns_test_hub + 1);
					p_dns_i->getaddrinfo( p_dns, p_pipe[ 1 ], p_test->domain, flags, NULL );
				}else{
					MOON_PRINT_MAN( ERROR, "pipe new error!" );
				}
				RECOVER_STRING( url, desc.domain_match.rm_eo, c );
				if( desc.port_match.rm_eo > desc.port_match.rm_so ){
					CUT_STRING( url, desc.port_match.rm_eo, c );
					printf( "port:%s\n", url + desc.port_match.rm_so );
					RECOVER_STRING( url, desc.port_match.rm_eo, c );
				}else{
					printf( "no port\n");
				}
				if( desc.path_match.rm_eo > desc.path_match.rm_so ){
					CUT_STRING( url, desc.path_match.rm_eo, c );
					printf( "path:%s\n", url + desc.path_match.rm_so );
					RECOVER_STRING( url, desc.path_match.rm_eo, c );
				}else{
					printf( "no path\n");
				}
			}else{
				printf( "url error!\n" );
			}
			j++;
		}else if( json_object_object_get_ex( p_obj, "cancel", &p_tmp ) == TRUE ){
			n = json_object_get_int( p_tmp );
			if( private_table[ n ] != NULL ){
				query_cancel( private_table[ n ] );
				CALL_INTERFACE_FUNC( private_table[ n ], gc_interface_s, ref_dec );
				private_table[ n ] = NULL;
			}
		}else if( json_object_object_get_ex( p_obj, "usleep", &p_tmp ) == TRUE ){
			sleep_us = json_object_get_int( p_tmp );
			usleep( sleep_us );
		}else{
			MOON_PRINT_MAN( ERROR, "unknow cmd" );
		}
	}
	json_object_put( p_dns_obj );
	sleep( 20 );
	for( i = 0; i < j; i++ ){
		if( private_table[ i ] != NULL ){
			query_cancel( private_table[ i ] );
			CALL_INTERFACE_FUNC( private_table[ i ], gc_interface_s, ref_dec );
			private_table[ i ] = NULL;
		}
	}
	return 0;
}

