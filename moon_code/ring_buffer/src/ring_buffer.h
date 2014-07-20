#ifndef _RING_BUFFER_H_
#define _RING_BUFFER_H_
#include"moon_interface.h"

enum {
	RING_NOWAIT = 0x1,
	RING_REPLACE_OLD = 0x2,
	RING_WAITALL = 0x4,
	RING_PEEK = 0x8,
	RING_TIMEOUT = 0x10,
	RING_MSGMODE = 0x20
};

void * ring_new( unsigned buffer_len, unsigned flags );

#endif
