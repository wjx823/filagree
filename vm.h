#ifndef VM_H
#define VM_H


#include <stdint.h>
#include <inttypes.h>
#include "struct.h"


#define ERROR_NULL    "null pointer"
#define ERROR_INDEX    "index out of bounds"


enum Opcode {
    VM_SET,
    VM_INT,
    VM_FLT,
    VM_STR,
    VM_VAR,
    VM_FNC,
    VM_LST,
    VM_MAP,
    VM_GET,
    VM_PUT,
    VM_ADD,
    VM_SUB,
    VM_MUL,
    VM_DIV,
    VM_NEG,
    VM_NOT,
    VM_EQ,
	VM_NEQ,
    VM_GT,
    VM_LT,
    VM_AND,
    VM_OR,
    VM_IF,
    VM_JMP,
    VM_CAL,
};

// variable ////////////////////////////////////////////////////////////////

enum VarType {
    VAR_INT,
    VAR_FLT,
    VAR_STR,
    VAR_FNC,
    VAR_LST,
    VAR_MAP,
	VAR_ERR,
    VAR_C,
};    

struct variable {
    const struct byte_array* name;
    uint8_t type;
    uint8_t marked;
    union {
        struct byte_array* str;
        struct array *list;
        int integer;
        float floater;
        void(*cfnc)(struct ifo*); // i.e., bridge
    };
    struct map *map;
};

const char* variable_value(const struct variable* v);
struct byte_array *variable_serialize(struct byte_array *bits,
									  const struct variable *in);
struct variable *variable_deserialize(struct byte_array *str); // todo: move current out of byte_array, so that I can make more const

typedef void(bridge)(struct ifo*);

#define ERROR_OPCODE "unknown opcode"

void display_program(const char* title, struct byte_array* program);
struct variable *execute(struct byte_array *program, bridge *callback_to_c);


#endif // VM_H
