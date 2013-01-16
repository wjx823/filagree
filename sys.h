#ifndef SYS_H
#define SYS_H

#include "vm.h"

struct variable *sys_find(context_p context, const struct byte_array *name);

struct variable *sys_func(struct context *context, struct byte_array *name);

struct variable *builtin_method(struct context *context,
								struct variable *indexable,
                                const struct variable *index);

#endif // SYS_H