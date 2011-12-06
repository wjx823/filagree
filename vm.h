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
    VM_SET,	//	pop a value and set a variable to it
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
	VM_ARG,	//	function arguments
	VM_RET,	//	return from a function,
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
        void(*cfnc)(struct lifo*); // i.e., bridge
    };
    struct map *map;
};

const char* variable_value(const struct variable* v);
struct byte_array *variable_serialize(struct byte_array *bits,
									  const struct variable *in);
struct variable *variable_deserialize(struct byte_array *str);

typedef void(bridge)(struct lifo*);

#define ERROR_OPCODE "unknown opcode"

void display_program(const char* title, struct byte_array* program);
struct variable *execute(struct byte_array *program, bridge *callback_to_c);


#endif // VM_H
