#ifndef UTIL_H
#define UTIL_H

#include <stdbool.h>
#include <setjmp.h>
#include <stdint.h>
#include <inttypes.h>


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
char *make_message(const char *fmt, va_list ap);
void assert_message(bool assertion, const char *format, ...);
void exit_message(const char *format, ...);
void null_check(const void* p);
int try();

// file

#define ERROR_FSIZE		"Could not get length of file"
#define ERROR_FOPEN		"Could not open file"
#define ERROR_FREAD		"Could not read file"
#define ERROR_FCLOSE	"Could not close file"

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

#define ERROR_ALLOC		"Could not allocate memory"



#endif // UTIL_H
