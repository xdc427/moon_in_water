#include<stdio.h>
#include<regex.h>
#include<string.h>
#include"moon_common.h"

static char * url_regex = "^((\\w+)://)?(((\\w+\\.)+\\w+)|(\\[([0-9a-fA-F:.]+)\\]))(:([1-9][0-9]{0,4}))?(/.*)?$";
static char * url_elem[ ] = { "", "", "scheame", "", "host_domain", "", "", "host_ipv6", "", "port", "path" };
static char * ipv6_regex = "^((([0-9A-Fa-f]{1,4}:){7}([0-9A-Fa-f]{1,4}|:))|(([0-9A-Fa-f]{1,4}:){6}(:[0-9A-Fa-f]{1,4}|((25[0-5]|2[0-4][0-9]|1[0-9][0-9]|[1-9]?[0-9])(\\.(25[0-5]|2[0-4][0-9]|1[0-9][0-9]|[1-9]?[0-9])){3})|:))|(([0-9A-Fa-f]{1,4}:){5}(((:[0-9A-Fa-f]{1,4}){1,2})|:((25[0-5]|2[0-4][0-9]|1[0-9][0-9]|[1-9]?[0-9])(\\.(25[0-5]|2[0-4][0-9]|1[0-9][0-9]|[1-9]?[0-9])){3})|:))|(([0-9A-Fa-f]{1,4}:){4}(((:[0-9A-Fa-f]{1,4}){1,3})|((:[0-9A-Fa-f]{1,4})?:((25[0-5]|2[0-4][0-9]|1[0-9][0-9]|[1-9]?[0-9])(\\.(25[0-5]|2[0-4][0-9]|1[0-9][0-9]|[1-9]?[0-9])){3}))|:))|(([0-9A-Fa-f]{1,4}:){3}(((:[0-9A-Fa-f]{1,4}){1,4})|((:[0-9A-Fa-f]{1,4}){0,2}:((25[0-5]|2[0-4][0-9]|1[0-9][0-9]|[1-9]?[0-9])(\\.(25[0-5]|2[0-4][0-9]|1[0-9][0-9]|[1-9]?[0-9])){3}))|:))|(([0-9A-Fa-f]{1,4}:){2}(((:[0-9A-Fa-f]{1,4}){1,5})|((:[0-9A-Fa-f]{1,4}){0,3}:((25[0-5]|2[0-4][0-9]|1[0-9][0-9]|[1-9]?[0-9])(\\.(25[0-5]|2[0-4][0-9]|1[0-9][0-9]|[1-9]?[0-9])){3}))|:))|(([0-9A-Fa-f]{1,4}:){1}(((:[0-9A-Fa-f]{1,4}){1,6})|((:[0-9A-Fa-f]{1,4}){0,4}:((25[0-5]|2[0-4][0-9]|1[0-9][0-9]|[1-9]?[0-9])(\\.(25[0-5]|2[0-4][0-9]|1[0-9][0-9]|[1-9]?[0-9])){3}))|:))|(:(((:[0-9A-Fa-f]{1,4}){1,7})|((:[0-9A-Fa-f]{1,4}){0,5}:((25[0-5]|2[0-4][0-9]|1[0-9][0-9]|[1-9]?[0-9])(\\.(25[0-5]|2[0-4][0-9]|1[0-9][0-9]|[1-9]?[0-9])){3}))|:)))(%.+)?$"; 
static char * ipv4_regex = "^((25[0-5]|2[0-4][0-9]|1[0-9][0-9]|[1-9][0-9]|[0-9])\\.){3}(25[0-5]|2[0-4][0-9]|1[09][0-9]|[1-9][0-9]|[0-9])$";

static void anlyse_url( char * url )
{
	regex_t regex, regex_ipv6, regex_ipv4;
	int len, i, ret;
	char c;
	regmatch_t match[ ARRAY_LEN( url_elem ) ];
	char error_buf[ 1024 ];

	memset( match, 0, sizeof( match ) );
	if( ( ret = regcomp( &regex, url_regex, REG_EXTENDED ) ) != 0 ){
		regerror( ret, &regex, error_buf, sizeof( error_buf ) );
		printf( "compile url error:%s\n", error_buf );
		return;
	}
	if( ( ret = regcomp( &regex_ipv4, ipv4_regex, REG_EXTENDED ) ) != 0 ){
		regerror( ret, &regex_ipv4, error_buf, sizeof( error_buf ) );
		printf( "compile ipv4 error:%s\n", error_buf );
		return;
	}
	if( ( ret = regcomp( &regex_ipv6, ipv6_regex, REG_EXTENDED ) ) != 0 ){
		regerror( ret, &regex_ipv6, error_buf, sizeof( error_buf ) );
		printf( "compile ipv6 error:%s\n", error_buf );
		return;
	}
	if( ( ret = regexec( &regex, url, ARRAY_LEN( match ), match, 0 ) ) != 0 ){
		regerror( ret, &regex, error_buf, sizeof( error_buf ) );
		printf( "url can't match:%s\n", error_buf );
		return;
	}
	for( i = 0; i < ARRAY_LEN( match ); i++ ){
		if( ( len = match[ i ].rm_eo - match[ i ].rm_so ) > 0 && url_elem[ i ][ 0 ] != '\0' ){
			CUT_STRING( url, match[ i ].rm_eo, c );
			printf( "%s:%s\n", url_elem[ i ], url + match[ i ].rm_so );
			if( strcmp( url_elem[ i ], "host_ipv6" ) == 0 ){
				ret = regexec( &regex_ipv6, url + match[ i ].rm_so, 0, NULL, 0 );
				if( ret != 0 ){
					printf( "host_ipv6 is invalid \n");
				}
			}else if( strcmp( url_elem[ i ], "host_domain" ) == 0 ){
				ret = regexec( &regex_ipv4, url + match[ i ].rm_so, 0, NULL, 0 );
				if( ret != 0 ){
					printf( "not a ipv4 address \n");
				}
			}
			RECOVER_STRING( url, match[ i ].rm_eo, c );
		}
	}
	regfree( &regex );
	return;
}

void main( int argc, char * argv[] )
{
	if( argc < 2 ){
		return;
	}
	anlyse_url( argv[ 1 ] );
}

