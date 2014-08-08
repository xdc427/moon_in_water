#ifndef _MOON_DNS_H_
#define _MOON_DNS_H_
#include<regex.h>
#include"moon_interface.h"

enum{
	DNS_FLAG_CLEAR_CACHE,
	DNS_FLAG_DONT_USE_CHACHE
};

typedef struct addr_elem_s{
	int len;
	int family;
	unsigned char addr_buf[ 16 ];
}addr_elem_s;
typedef addr_elem_s * addr_elem;

typedef struct{
	int ( *get_num )( void * p_data );
	int ( *get_timestamp )( void * p_data, struct timeval * p_tv );
	addr_elem ( *get_next )( void * p_data, addr_elem p_elem );
	addr_elem ( *get_prev )( void * p_data, addr_elem p_elem );
} dns_addrs_interface_s;
typedef dns_addrs_interface_s * dns_addrs_interface;
DECLARE_INTERFACE( dns_addrs_interface_s );

typedef struct url_desc_s{
	regmatch_t scheame_match;
	regmatch_t domain_match;
	regmatch_t port_match;
	regmatch_t path_match;
} url_desc_s;
typedef url_desc_s * url_desc;

void * get_dnsinstance();
	
typedef struct dns_interface_s{
	int ( *analyse_url )( void * p_dns, const char * url, url_desc p_udesc );
	int ( *getaddrinfo )( void * p_dns, void * p_pipe, const char * domain, int flags, struct timeval * p_tv );
} dns_interface_s;
typedef dns_interface_s * dns_interface;
DECLARE_INTERFACE( dns_interface_s );

typedef struct dns_listener_interface_s{
	void ( *on_getaddrinfo )( void * p_data, void * p_pipe, void * p_addrs );
} dns_listener_interface_s;
typedef dns_listener_interface_s * dns_listener_interface;
DECLARE_INTERFACE( dns_listener_interface_s );

#endif

