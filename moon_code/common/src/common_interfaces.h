#ifndef _COMMON_INTERFACES_
#define _COMMON_INTERFACES_
#include"moon_interface.h"

typedef struct gc_interface_s{
	void ( *ref_inc )( void * );
	void ( *ref_dec )( void * );
} gc_interface_s;
typedef gc_interface_s * gc_interface;
DECLARE_INTERFACE( gc_interface_s );

#define GC_REF_INC( p_data ) ({\
	__sync_add_and_fetch( &( p_data )->ref_num, 1 );\
})

#define GC_REF_DEC( p_data ) ({\
	__sync_sub_and_fetch( &( p_data )->ref_num, 1 );\
})

typedef struct io_interface_s{
	int ( *read )( void * p_io, char *out, unsigned len, unsigned flags, ... );
	int ( *write )( void * p_io, char *in, unsigned len, unsigned flags, ... );
	void ( *close )( void * p_io );
	int ( *control )( void * p_io, int cmd, ... );
} io_interface_s;
typedef io_interface_s * io_interface;
DECLARE_INTERFACE( io_interface_s );

typedef struct io_pipe_interface_s{
	int ( *read )( void * p_io, void * p_pipe
		, char *out, int len, int flags, ... );
	int ( *write )( void * p_io, void * p_pipe
		, char *in, int len, int flags, ... );
	void ( *close )( void * p_io, void * p_pipe );
	int ( *control )( void * p_io, void * p_pipe, int cmd, ... );
} io_pipe_interface_s;
typedef io_pipe_interface_s * io_pipe_interface;
DECLARE_INTERFACE( io_pipe_interface_s );

typedef struct io_listener_interface_s{
	int ( *send_event )( void * p_datai, void * p_pipe );
	int ( *recv_event )( void * p_data, void * p_pipe );
	void ( *close_event )( void * p_data, void * p_pipe );
} io_listener_interface_s;
typedef io_listener_interface_s * io_listener_interface;
DECLARE_INTERFACE( io_listener_interface_s );

//事件触发程序有两种构建方式：
//1，虚拟式，从上至下一步步虚拟。这种方式有简洁一致的接口，但浪费资源。
//2. 预约式，需要什么资源首先预约，当资源配齐时再构造。这种方法节省资源但需要新增接口。
//下面是预约接口
typedef struct order_listener_interface_s{
	int ( *on_ready )( void * p_data, void * p_pipe, void * p_new_pipe );
} order_listener_interface_s;
typedef order_listener_interface_s * order_listener_interface;
DECLARE_INTERFACE( order_listener_interface_s );

#endif

