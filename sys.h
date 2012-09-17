#ifndef SYS_H
#define SYS_H

#include "vm.h"

/*
void print();
void save();
void load();
void rm();
*/

struct variable *sys_find(context_p context, const struct byte_array *name);

struct variable *sys_func(struct Context *context, struct byte_array *name);

struct variable *builtin_method(struct Context *context,
								struct variable *indexable,
                                const struct variable *index);

#endif // SYS_H