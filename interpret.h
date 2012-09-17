//
//  interpret.h
//  filagree
//

#ifndef INTERPRET_H
#define INTERPRET_H

#include "vm.h"

struct variable *interpret_file(const struct byte_array *filename, find_c_var *find);
struct variable *interpret_string(const char *str, find_c_var *find);

#endif // INTERPRET_H
