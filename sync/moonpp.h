#define SHADOW_GET( p_data ) ({ \
	typeof ( p_data ) p_tmp; \
	p_tmp = shadow_runtime( p_data, sizeof( *p_data ), "r" ); \
	*( p_tmp ); \
})

#define SHADOW_MEMGET( p_data, dst, len ) {\
	typeof ( p_data ) p_tmp; \
	p_tmp = shadow_runtime( p_data, len, "r" ); \
	memcpy( dst, p_tmp, len); \
}

#define SHADOW_SET( p_data, num ) {\
	typeof ( p_data ) p_tmp; \
	p_tmp = shadow_runtime( p_data, sizeof( *p_data ), "w" ); \
	*( p_tmp ) = num; \
}

#define SHADOW_MEMSET( p_data, src, len ) {\
	typeof ( p_data ) p_tmp; \
	p_tmp = shadow_runtime( p_data, len, "w"); \
	memcpy( p_tmp, src, len ); \
}

#define SHADOW_ADDRESS( p_data ) ({ \
	p_data; \
})

//有可能为shadow_point address
#define SHADOW_POINT_CMP( p_data, opt, cmp ) ({ \
	typeof ( p_data ) p_tmp; \
	p_tmp = shadow_runtime( p_data, sizeof( *p_data ), "c" ); \
	( *( p_tmp ) opt cmp ); \
})

#define SHADOW_VAR 
#define SHADOW_POINT 
#define SHADOW_STRUCT 
#define SHADOW_NEW( type, n ) shadow_new( #type, sizeof( type ) * n )
#define SHADOW_DEL( shadow_point ) shadow_del( shadow_point )

