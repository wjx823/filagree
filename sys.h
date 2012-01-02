#ifndef SYS_H
#define SYS_H

#include "vm.h"

bridge *callback2c;

void print();
void save();
void load();
void rm();

struct string_func
{
	const char* name;
	bridge* func;
};

struct variable *func_map();


#endif // SYS_H