#include<stdio.h>
#include<pthread.h>

int n;
pthread_key_t key;

void * fun( void * arg )
{
	int n;
	int *pn;

	pthread_setspecific( key, &n );
	pn = ( int * )pthread_getspecific( key );
	*pn = pthread_self();
	printf( "write:%u\n", pthread_self() );
	sleep( 5 );
	printf("%u:%u:%p:%p\n", pthread_self(), *pn, &n, pn );
}

void main()
{
	pthread_t pt[10];
	int i;
	void * ret;

	pthread_key_create( &key, NULL );
	for( i = 0; i < 10; i++ ){
		pthread_create( pt + i, NULL, fun, NULL );
	}
	for( i = 0; i < 10; i++ ){
		pthread_join( *( pt + i ), &ret );
	}
	pthread_key_delete( key );
}
