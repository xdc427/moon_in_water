#ifndef _SHADOW_STRUCT_H_
#define _SHADOW_STRUCT_H_

struct struct_all_s;
typedef struct struct_all_s struct_all_s;
typedef struct struct_all_s * struct_all;

struct block_copy_s{
	int flag;//flag=0时：从src到dst，并且从src中提取指针到points中，
			 //flag=1时：从src到dst，并且从points中恢复指针到dst中。
	void *src;
	void *dst;
	void ** points;
	struct_all src_type;
	struct_all dst_type;
	int index;
	int num;
};
typedef struct block_copy_s block_copy_s;
typedef struct block_copy_s * block_copy;

int find_type( struct_all head, char * type );
void complete_var_type( struct_all head );
int get_block_copy( block_copy p_copy );
int unsigned_int_copy( block_copy p_copy );
int signed_int_copy( block_copy p_copy );
int point_copy( block_copy p_copy );
int float_copy( block_copy p_copy );
int get_target_offset( struct_all p_src_all, struct_all p_dst_all, int offset, int type_index, int num );
int get_struct_len( struct_all p_all, int type_index );
int get_struct_points_num( struct_all p_all, int type_index );
void print_struct_all( struct_all );
struct_all dump_struct_all( struct_all );
void free_struct_all( struct_all );

#ifdef LEVEL_TEST
void set_type_size( struct_all p_all, char * type, int size );
#endif

#endif

