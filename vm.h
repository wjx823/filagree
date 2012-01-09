#ifndef VM_H
#define VM_H


#include <stdint.h>
#include <inttypes.h>
#include "struct.h"


#define ERROR_NULL    "null pointer"
#define ERROR_INDEX    "index out of bounds"


enum Opcode {
	VM_NIL,	//	push nil
    VM_INT,	//	push an integer
    VM_FLT,	//	push a float
	VM_BOOL,//	push a boolean
    VM_STR,	//	push a string
    VM_VAR,	//	push a variable
    VM_FNC,	//	push a function
    VM_DST,	//	done with assignment
    VM_SET, //  set a variable
	VM_SRC,	//	push a set of values
    VM_LST,	//	push a list
    VM_MAP,	//	push a map
    VM_GET,	//	get an item from a list or map
    VM_PUT,	//	put an item in a list or map
    VM_ADD,	//	add two values
    VM_SUB,	//	subtract two values
    VM_MUL,	//	multiply two values
    VM_DIV,	//	divide two values
    VM_NEG,	//	arithmetic negate a value
    VM_NOT,	//	boolean negate a value
    VM_EQU,	//	compare
	VM_NEQ,	//	diff
    VM_GT,	//	greater than
    VM_LT,	//	less than
    VM_AND,	//	logical and
    VM_OR,	//	logical or
    VM_IF,	//	if then
    VM_JMP,	//	jump the program counter
    VM_CAL,	//	call a function
	VM_MET,	//	call an object method
	VM_RET,	//	return from a function,
	VM_ITR,	//	iteration loop
	VM_COM,	//	comprehension
};

// variable ////////////////////////////////////////////////////////////////

enum VarType {
	VAR_NIL,
    VAR_INT,
    VAR_FLT,
	VAR_BOOL,
    VAR_STR,
    VAR_FNC,
    VAR_LST,
    VAR_MAP,
	VAR_ERR,
    VAR_C,
};    

struct variable {
    const struct byte_array* name;
    enum VarType type;
    uint8_t marked;
    union {
        struct byte_array* str;
        struct array *list;
        int integer;
        float floater;
		bool boolean;
        void(*cfnc)(struct stack*); // i.e., bridge
    };
    struct map *map;
};

const char* variable_value(const struct variable* v);
struct byte_array *variable_serialize(struct byte_array *bits,
									  const struct variable *in);
struct variable *variable_deserialize(struct byte_array *str);

typedef void(bridge)(struct stack*);

#define ERROR_OPCODE "unknown opcode"

void display_program(const char* title, struct byte_array* program);
struct variable *execute(struct byte_array *program, bridge *callback_to_c);

extern int variable_save(const struct variable* v, const struct variable* path);
extern struct variable *variable_load(const struct variable* path);
struct variable *variable_new_err(const char* message);
struct variable *variable_new_c(bridge *cfnc);
struct variable *variable_new_int(int32_t i);
struct variable *variable_new_nil();
struct variable *variable_new_map(struct map *map);
struct variable *variable_new_float(float f);
struct variable *variable_new_str(struct byte_array *str);
struct variable *variable_new_fnc(struct byte_array *fnc);
struct variable *variable_new_list(struct array *list);
void vm_call();
const char *var_type_str(enum VarType vt);
void vm_exit_message(const char *format, ...);
void vm_null_check(const void* p);

#endif // VM_H
