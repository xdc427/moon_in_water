struct struct_all_s{
	unsigned long num;
};
typedef struct struct_all_s struct_all_s;
typedef struct struct_all_s * struct_all;

struct struct_one_s{
	char name[256];
	unsigned long size;
	unsigned long num;
};
typedef struct struct_one_s struct_one_s;
typedef struct struct_one_s * struct_one;

struct var_s{
	char name[256];
	char type[256];
	unsigned long array_d; //数组维度，0为非数组。
	unsigned long size;
	unsigned long addr;
};
typedef struct var_s var_s;
typedef struct var_s * var;

