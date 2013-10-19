struct struct_one_s{
	char name[256]; //正则表达式的描述
	unsigned long size;
	unsigned long num;//num为0时为基本结构
};
typedef struct struct_one_s struct_one_s;
typedef struct struct_one_s * struct_one;

struct var_s{
	char name[256];
	char type[256];
	unsigned int  type_num;//指向的类型描述
	unsigned long array_d; //数组维度，0为非数组。
	unsigned long array_elem_size;
	unsigned long size;
	unsigned long addr;
	unsigned long is_hide;//1为隐藏，0为可见
};
typedef struct var_s var_s;
typedef struct var_s * var;

//在打包传输内存快时，先传所有基本类型的长度，每个内存快含有一个类型号
