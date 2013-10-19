struct struct_all_s{ //struct_all_s + struct_one[] + ( struct_one_s + vvar_s[] )[]
    unsigned long num;
};
typedef struct struct_all_s struct_all_s;
typedef struct struct_all_s * struct_all;

int find_type( struct_all head, char * type );
void complete_var_type( struct_all head );
