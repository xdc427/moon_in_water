#include<stdio.h>

struct has_union_s{
	int a;
	union{
		void *addr;
		struct{
			unsigned long base;
			unsigned long offset;
		}bo_s;
	};
};
int arr[10];

union {
	int * addr;
	struct{
		int * pending;
		unsigned long base;
		unsigned long offset;
	}addition;
};

void main()
{
	struct has_union_s tmp;

	tmp.a = 10;
	tmp.addr = 100;

	addr = arr;
	addr[ 1 ] =12;

	printf( "%p:%d:%p:%d\n", &tmp.addr, sizeof( tmp.addr )
			, &tmp.bo_s, sizeof( tmp.bo_s ) );
	printf( "%p:%p\n", &arr, arr );
} 
