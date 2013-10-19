#ifndef _RING_BUFFER_H_
#define _RING_BUFFER_H_

enum {
	RING_NOWAIT = 0x1,
	RING_REPLACE_OLD = 0x2,
	RING_WAITALL = 0x4,
	RING_PEEK = 0x8,
	RING_TIMEOUT = 0x10
};

int ring_new( unsigned int buffer_len, unsigned int flags );
int ring_join( int fd );
void ring_leave( int fd, int is_destroy );
int ring_read( int fd, char *out, unsigned int len, unsigned int flags, ... );
int ring_write( int fd, char *in, unsigned int len, unsigned int flags, ... );

#endif
