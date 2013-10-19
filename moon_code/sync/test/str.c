#include<stdio.h>

char *a1 = "hello";
char a2[10] = "hello";
char b1[][6] = { "hello", "asdfgg" };
char **b2 = { "hello", "asdfgg" };
char * b3[] = { "hello", "asdfgg" };

void main()
{
	long  double static a;
	printf( "a:%d:%d\n", sizeof( a1 ), sizeof( a2 ) );
	printf( "b:%d:%d:%d\n", sizeof( b1 ), sizeof( b2 ), sizeof( b3 ) );
	printf( "%p:%p\n",b2,b2[0]);
}
