#ifndef VM_H
#define VM_H


#include <stdint.h>
#include <inttypes.h>
#include "struct.h"
#include "util.h"
#include "variable.h"

#define ERROR_NULL  "null pointer"
#define ERROR_INDEX "index out of bounds"

struct Context {
    struct program_state *vm_state;
    struct stack *program_stack;
    struct stack *operand_stack;
    //struct array *args;
    //struct stack *rhs;
    struct variable *vm_exception;
    bridge *callback2c;
    bool runtime;
    bool done;
    uint32_t num_vars;
    struct variable* error;
    uint8_t indent;
};

struct program_state {
    //    struct byte_array *code;
    struct array *args;
    struct map *named_variables;
    struct array *all_variables;
    uint32_t pc;
};

enum Opcode {
    VM_NIL, // push nil
    VM_INT, // push an integer
    VM_FLT, // push a float
    VM_BUL, // push a boolean
    VM_STR, // push a string
    VM_VAR, // push a variable
    VM_FNC, // push a function
    VM_DST, // done with assignment
    VM_SET, // set a variable
    VM_SRC, // push a set of values
    VM_LST, // push a list
    VM_MAP, // push a map
    VM_GET, // get an item from a list or map
    VM_PUT, // put an item in a list or map
    VM_ADD, // add two values
    VM_SUB, // subtract two values
    VM_MUL, // multiply two values
    VM_DIV, // divide two values
    VM_MOD, // modulo
    VM_BND, // bitwise and
    VM_BOR, // bitwise or
    VM_INV, // bitwise inverse
    VM_XOR, // xor
    VM_LSF, // left shift
    VM_RSF, // right shift
    VM_NEG, // arithmetic negate a value
    VM_NOT, // boolean negate a value
    VM_EQU, // compare
    VM_NEQ, // diff
    VM_GTN, // greater than
    VM_LTN, // less than
    VM_AND, // logical and
    VM_OR,  // logical or
    VM_IFF, // if then
    VM_JMP, // jump the program counter
    VM_CAL, // call a function
    VM_MET, // call an object method
    VM_RET, // return from a function,
    VM_ITR, // iteration loop
    VM_COM, // comprehension
    VM_TRY, // try.catch
    VM_TRO, // throw
};

#define ERROR_OPCODE "unknown opcode"

#ifdef DEBUG
void display_program(struct byte_array* program);
#endif
struct Context *vm_init();
struct variable *execute(struct byte_array *program,
                         bool in_context,
                         bridge *callback_to_c);
void garbage_collect(struct Context *context);

void vm_call(struct Context *context);
void *vm_exit_message(struct Context *context, const char *format, ...);
void vm_null_check(struct Context *context, const void* p);
void vm_assert(struct Context *context, bool assertion, const char *format, ...);

#endif // VM_H
