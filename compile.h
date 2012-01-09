#ifndef COMPILE_H

#include "vm.h"

struct variable *interpret_file(const char *filename, bridge *callback);
struct variable *interpret_string(const char *string, bridge *callback);

#endif // COMPILE_H
