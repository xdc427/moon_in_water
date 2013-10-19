#include<stdio.h>
#include<regex.h>

void main( int argc, char * argv[] )
{
	if( argc < 3 ){
		return;
	}

	regex_t regex;
	size_t len;
	int err;

	regcomp( &regex, argv[ 1 ], REG_EXTENDED );
	err = regexec( &regex, argv[2], 0, NULL, 0 );
	regfree( &regex );
	printf( "%d\n", err );
}

