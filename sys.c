#include "sys.h"
#include "struct.h"
#include "vm.h"
#include <stdio.h>
#include "util.h"

extern struct stack *rhs;
extern struct stack *operand_stack;
extern void src_size(int32_t size);
extern void call(struct byte_array *program);

void print()
{
	stack_pop(rhs); // self
	struct variable *v;
	while ((v = stack_pop(rhs)))
		printf("%s\n", variable_value(v));
}

void save()
{
	stack_pop(rhs); // self
	struct variable *v = stack_pop(rhs);
	struct variable *path = stack_pop(rhs);
	variable_save(v, path);
}

void load()
{
	stack_pop(rhs); // self
	struct variable *path = stack_pop(rhs);
	struct variable *v = variable_load(path);

	if (v)
		stack_push(operand_stack, v);
}

void rm()
{
	stack_pop(rhs); // self
	struct variable *path = stack_pop(rhs);
	remove(byte_array_to_string(path->str));
}

struct string_func builtin_funcs[] = {
	{"print", &print},
	{"save", &save},
	{"load", &load},
	{"remove", &rm},
};

struct variable *func_map()
{
	struct map *map = map_new();
	for (int i=0; i<ARRAY_LEN(builtin_funcs); i++) {
		struct byte_array *name = byte_array_from_string(builtin_funcs[i].name);
		struct variable *value = variable_new_c(builtin_funcs[i].func);
		map_insert(map, name, value);
	}
	return variable_new_map(map);
}
