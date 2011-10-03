#ifndef VM_H
#define VM_H


#include <stdint.h>
#include <stdbool.h>
#include <inttypes.h>
#include <setjmp.h>
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
    char* name;
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

// c bridge ////////////////////////////////////////////////////////////////

typedef void(bridge)(struct ifo*);

struct number_string {
    uint8_t number;
    char* chars;
};

const char* num_to_string(const struct number_string *ns, int num_items, int num);

#define ERROR_OPCODE "unknown opcode"

void display_program(const char* title, struct byte_array* program);
struct variable *execute(struct byte_array *program, bridge *callback_to_c);


#ifdef __LP64__
#define VOID_INT int64_t
#define VOID_FLT long double
#else
#define VOID_INT int32_t
#define VOID_FLT double)(int32_t
#endif

#define ARRAY_LEN(x) (sizeof x / sizeof *x)

#ifdef DEBUG

#ifdef ANDROID

#include <android/log.h>
#define TAG "fisil"
#define LOG_LINE_LENGTH 100
char log_message[LOG_LINE_LENGTH];
#define DEBUGPRINT(...) { snprintf(log_message, LOG_LINE_LENGTH, __VA_ARGS__ );\
__android_log_write(ANDROID_LOG_ERROR, TAG, log_message); }

#else // not ANDROID

#include <stdio.h>
#define DEBUGPRINT(...) fprintf( stderr, __VA_ARGS__ );

#endif // (not) ANDROID

#else // (not) DEBUG

#define DEBUGPRINT(...)

#endif // DEBUG

#define ITOA_LEN    19 // enough for 64-bit integer

extern jmp_buf trying;
void assert_message(bool assertion, const char* message);
void exit_message(const char* message);
void null_check(const void* p);
int try();


#endif // VM_H
