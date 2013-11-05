#include<stdio.h>

void func( int a ){
	printf( "%d\n", a );
}
void main()
{
	int a;
	unsigned long b;
	long c;

	b = -( ( unsigned long)1 << 30 ) - ( ( unsigned long )1 << 31 ) + 1;
	a = b;
	func( a );
	a = -1;
	c = a;
	printf( "%lx\n", c );
}
