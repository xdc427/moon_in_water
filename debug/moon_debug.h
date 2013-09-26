#ifndef _MOON_DEBUG_H_
#define _MOON_DEBUG_H_

#define MODENAME ""

#ifdef LEVEL_NORMAL
#define MOON_PRINT_NORMAL( level, xid, body, ... ) moon_print( MODENAME, level, xid, body ,##__VA_ARGS__ )
#define MOON_NORMAL( code ) code
#define LEVEL_DEBUG
#define LEVEL_WARNNING
#define LEVEL_ERROR
#else
#define MOON_PRINT_NORMAL( level, xid, body, ... ) 
#define MOON_NORMAL( code ) 
#endif

#ifdef LEVEL_DEBUG
#define MOON_PRINT_DEBUG( level, xid, body, ... ) moon_print( MODENAME, level, xid, body ,##__VA_ARGS__ )
#define MOON_DEBUG( code ) code
#define LEVEL_WARNNING
#define LEVEL_ERROR
#else
#define MOON_PRINT_DEBUG( level, xid, body, ... ) 
#define MOON_DEBUG( code ) 
#endif

#ifdef LEVEL_WARNNING
#define MOON_PRINT_WARNNING( level, xid, body, ... ) moon_print( MODENAME, level, xid, body ,##__VA_ARGS__ )
#define MOON_WARNNING( code ) code
#define LEVEL_ERROR
#else
#define MOON_PRINT_WARNNING( level, xid, body, ... ) 
#define MOON_WARNNING( code ) 
#endif

#ifdef LEVEL_ERROR
#define MOON_PRINT_ERROR( level, xid, body, ... ) moon_print( MODENAME, level, xid, body ,##__VA_ARGS__ )
#define MOON_ERROR( code ) code
#else
#define MOON_PRINT_ERROR( level, xid, body, ... ) 
#define MOON_ERROR( code ) 
#endif

#ifdef LEVEL_TEST
#define MOON_PRINT_TEST( level, xid, body, ... ) moon_print( MODENAME, level, xid, body ,##__VA_ARGS__ )
#define MOON_TEST( code ) code
#else
#define MOON_PRINT_TEST( level, xid, body, ... ) 
#define MOON_TEST( code ) 
#endif

#define MOON_PRINT( level, xid, body, ... ) MOON_PRINT_##level( #level, xid, body ,##__VA_ARGS__ )

enum{
	//group one
	ERROR ,
	WARNNING,
	DEBUG,
	NORMAL,
	//group two
	TEST 
	//group end
};

void moon_print( char * name, char * level, char * xid, char * body, ... );

#endif
