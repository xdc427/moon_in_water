#include<stdio.h>
#include<netdb.h>
#include<netinet/in.h>
#include<arpa/inet.h>
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
		CALL_INTERFACE_FUNC( p_test, gc_interface_s, ref_dec );
	}
}

static void query_fail( void * p_data, void * p_pipe )
{
	test_private p_test;
	int tmp;
	
	p_test = p_pipe;
	tmp = __sync_fetch_and_add( &p_test->status, 1 );
	if( tmp == 0 ){
		printf( "domain fail: %s\n", p_test->domain );
		CALL_INTERFACE_FUNC( p_test, gc_interface_s, ref_dec );
	}
}

int main( int argc, char * argv[] )
{
	void * p_dns;
	url_desc_s desc;
	dns_interface p_dns_i;
	int ret, len;
	char c;
	void * p_pipe[ 2 ];
	test_private p_test;

	if( argc < 2 ){
		return 1;
	}
	p_dns = get_dnsinstance();
	p_dns_i = FIND_INTERFACE( p_dns, dns_interface_s );
	ret = p_dns_i->analyse_url( p_dns, argv[ 1 ], &desc );
	if( ret == 0 ){
		if( desc.scheame_match.rm_eo > desc.scheame_match.rm_so ){
			CUT_STRING( argv[ 1 ], desc.scheame_match.rm_eo, c );
			printf( "scheame:%s\n", argv[ 1 ] + desc.scheame_match.rm_so );
			RECOVER_STRING( argv[ 1 ], desc.scheame_match.rm_eo, c );
		}else{
			printf( "no scheame\n");
		}
		CUT_STRING( argv[ 1 ], desc.domain_match.rm_eo, c );
		printf( "domain:%s\n", argv[ 1 ] + desc.domain_match.rm_so );
		len = 0;
		CALL_INTERFACE_FUNC( p_dns, pipe_listener_interface_s, get_pipe_data_len, &len );
		ret = pipe_new( p_pipe, sizeof( test_private_s ), len, 0 );
		if( ret >= 0 ){
			p_test = p_pipe[ 0 ];
			snprintf( p_test->domain, sizeof( p_test->domain )
				, "%s", argv[ 1 ] + desc.domain_match.rm_so );
			CALL_INTERFACE_FUNC( p_pipe[ 0 ], pipe_interface_s, set_point_ref, &dns_test_hub + 1);
			p_dns_i->getaddrinfo( p_dns, p_pipe[ 1 ], p_test->domain, 0, NULL );
		}else{
			MOON_PRINT_MAN( ERROR, "pipe new error!" );
		}
		RECOVER_STRING( argv[ 1 ], desc.domain_match.rm_eo, c );
		if( desc.port_match.rm_eo > desc.port_match.rm_so ){
			CUT_STRING( argv[ 1 ], desc.port_match.rm_eo, c );
			printf( "port:%s\n", argv[ 1 ] + desc.port_match.rm_so );
			RECOVER_STRING( argv[ 1 ], desc.port_match.rm_eo, c );
		}else{
			printf( "no port\n");
		}
		if( desc.path_match.rm_eo > desc.path_match.rm_so ){
			CUT_STRING( argv[ 1 ], desc.path_match.rm_eo, c );
			printf( "path:%s\n", argv[ 1 ] + desc.path_match.rm_so );
			RECOVER_STRING( argv[ 1 ], desc.path_match.rm_eo, c );
		}else{
			printf( "no path\n");
		}
	}else{
		printf( "url error!\n" );
	}

	while( 1 );

	return 0;
}

