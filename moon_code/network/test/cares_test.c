#include<sys/time.h>
#include<stdio.h>
#include<netdb.h>
#include<arpa/inet.h>
#include<ares.h>

void dns_callback ( void* arg, int status, int timeouts, struct hostent* host )
{
	char ip[ 1024 ];
	int i;

	if( status == ARES_SUCCESS ){
		for( i = 0; host->h_addr_list[ i ] != NULL; i++ ){
			inet_ntop( host->h_addrtype, host->h_addr_list[ i ], ip, sizeof( ip ) );
			printf( "%s	%d\n", ip, host->h_length );
		}
	}else{
		printf( "lookup failed:%d\n", status );
	}
}

void main_loop( ares_channel channel )
{
	int nfds, count;
	fd_set readers, writers;
	struct timeval tv, *tvp;
	while( 1 ){
		FD_ZERO( &readers );
		FD_ZERO( &writers );
		nfds = ares_fds( channel, &readers, &writers );
		if( nfds == 0 )
			break;
		tvp = ares_timeout( channel, NULL, &tv );
		count = select( nfds, &readers, &writers, NULL, tvp );
		ares_process( channel, &readers, &writers );
	}
}

int main( int argc, char *argv[] )
{
	int i, res, version;
	char * verstr;

	if( argc < 2 ) {
		printf( "input address!\n" );
		return 1;
	}
	ares_channel channel;
	if( ( res = ares_init( &channel ) ) != ARES_SUCCESS ){
		printf( "ares feiled:%d", res );
		return 1;
	}
	verstr = ares_version( &version );
	printf( "%s	%d\n", verstr, version );
	for( i = 0; i < 2; i++ ){
		ares_gethostbyname( channel, argv[ 1 ], AF_UNSPEC, dns_callback, NULL );
	}
	main_loop( channel );
	return 0;
}

