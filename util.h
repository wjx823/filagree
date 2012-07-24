#ifndef UTIL_H
#define UTIL_H

#include <stdbool.h>
#include <setjmp.h>
#include <stdint.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>

#ifdef __LP64__
#define VOID_INT int64_t
#define VOID_FLT long double
#else
#define VOID_INT int32_t
#define VOID_FLT double)(int32_t
#endif

#ifdef MBED
#pragma diag_suppress 1293  //  suppress squeamish warning of "assignment in condition"
#endif

#define ARRAY_LEN(x) (sizeof x / sizeof *x)
#define ITOA_LEN    19 // enough for 64-bit integer

extern jmp_buf trying;
const char *make_message(const char *fmt, va_list ap);
void assert_message(bool assertion, const char *format, ...);
void *exit_message(const char *format, ...);
void null_check(const void* p);
void log_print(const char *format, ...);

#ifdef DEBUG
#define DEBUGPRINT(...) log_print( __VA_ARGS__ );
#else
#define DEBUGPRINT(...) {};
#endif // #ifdef DEBUG

// file

#define ERROR_FSIZE     "Could not get length of file"
#define ERROR_FOPEN     "Could not open file"
#define ERROR_FREAD     "Could not read file"
#define ERROR_FCLOSE    "Could not close file"

struct byte_array *read_file(const struct byte_array *filename);
int write_file(const struct byte_array* filename, struct byte_array* bytes);
long fsize(FILE* file);

struct number_string {
    uint8_t number;
    char* chars;
};

const char* num_to_string(const struct number_string *ns, int num_items, int num);
#define NUM_TO_STRING(ns, num) num_to_string(ns, ARRAY_LEN(ns), num)

// error messages

#define ERROR_ALLOC        "Could not allocate memory"



#endif // UTIL_H
