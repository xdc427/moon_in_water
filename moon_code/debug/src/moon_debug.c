#include<stdio.h>
#include<sys/time.h>
#include<stdarg.h>
#include"moon_debug.h"

/*
 *output: modename:level:xid:time:body
 */
#define STRING_NULL ""
#define REPLACE_NULL( str ) ( str == NULL ? STRING_NULL : str )

void moon_print_safe( const char * name, const char *level, const char * xid, struct timeval * tv, const char * body, ... )
{
	va_list vl;
	char fmt[1024];

	va_start( vl, body );
	snprintf( fmt, sizeof( fmt ), "%s:%s:%s:%ld%06ld:%s\n"
			, REPLACE_NULL( name ), REPLACE_NULL( level )
			, REPLACE_NULL( xid ), tv->tv_sec, tv->tv_usec, REPLACE_NULL( body ) );
	vprintf( fmt, vl );
	va_end( vl );
	fflush( stdout );
}

void moon_print( const char * name, const char *level, const char * xid, const char * body, ... )
{
	va_list vl;
	struct timeval now;
	char fmt[1024];

	va_start( vl, body );
	gettimeofday( &now, NULL );
	snprintf( fmt, sizeof( fmt ), "%s:%s:%s:%ld%06ld:%s\n"
			, REPLACE_NULL( name ), REPLACE_NULL( level )
			, REPLACE_NULL( xid ), now.tv_sec, now.tv_usec, REPLACE_NULL( body ) );
	vprintf( fmt, vl );
	va_end( vl );
	fflush( stdout );
}

