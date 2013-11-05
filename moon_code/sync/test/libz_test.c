#include<stdio.h>
#include"zlib.h"
#include<string.h>

void main()
{
	char test_str[] = "hello hello aaaaaaaaaaaaaabbbbbbbbbbbbbccaaddd";
	z_stream stream;
	int ret;
	char in[ sizeof( test_str ) ];
	char out[ sizeof( test_str ) ]; 
	int out_len;

	stream.zalloc = Z_NULL;
	stream.zfree = Z_NULL;
	stream.opaque = Z_NULL;
	//compress
	//ret = deflateInit( &stream, Z_DEFAULT_COMPRESSION );
	ret = deflateInit2( &stream, Z_DEFAULT_COMPRESSION, Z_DEFLATED, -MAX_WBITS, 8, Z_DEFAULT_STRATEGY );
	if( ret != Z_OK ){
		printf( "init error\n" );
		return;
	}
	stream.avail_in = sizeof( test_str );
	stream.next_in = test_str;
	stream.avail_out = sizeof( out );
	stream.next_out = out;
	ret = deflate( &stream, Z_FINISH );
	if( ret != Z_STREAM_END ){
		printf( "compress error\n");
		return;
	}
	deflateEnd( &stream );
	printf( "compress size : %d:%d:%d:%d\n", sizeof( test_str ), stream.avail_in, stream.total_in, stream.total_out);
	out_len = stream.total_out; 
	//uncompress
	memset( &stream, 0, sizeof( stream ) );
//	ret = inflateInit( &stream );
	ret = inflateInit2( &stream, -MAX_WBITS );
	if( ret != Z_OK ){
		printf( "init second error \n" );
		return;
	}
	stream.avail_in = out_len;
	stream.next_in = out;
	stream.avail_out = sizeof( in );
	stream.next_out = in;
	ret = inflate( &stream, Z_NO_FLUSH );
	if( ret != Z_STREAM_END ){
		printf( "uncompress error\n");
		return;
	}
	inflateEnd( &stream );
	printf( "%d:%d:%s\n", stream.total_in, stream.total_out, in );
	if( strncmp( test_str, in, sizeof( test_str ) ) != 0 ){
		printf( "compress-uncompress error\n" );
	}else{
		printf( "compress-uncompress success\n" );
	}

}
