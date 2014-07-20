#ifndef _SHADOW_BASE_H_
#define _SHADOW_BASE_H_

struct struct_all_s{ //struct_all_s + struct_one[] + ( struct_one_s + var_s[] )[]
    int num;
	int size;
	unsigned char md5_sum[ 16 ];
};

struct struct_one_s{
	char name[256]; //正则表达式的描述
	int size;
	int num;//num为0时为基本结构
	int point_num;//初始为-1
	int ( * copy_func )( struct block_copy_s * );
};
typedef struct struct_one_s struct_one_s;
typedef struct struct_one_s * struct_one;

struct var_s{
	char name[256];
	char type[256];
	int  type_num;//指向的类型描述
	int array_d; //数组维度，0为非数组。
	int array_elem_size;
	int size;
	int len;
	int addr;
	int point_addr;//指针数量的偏移
	int is_hide;//1为隐藏，0为可见
};
typedef struct var_s var_s;
typedef struct var_s * var;

#endif
