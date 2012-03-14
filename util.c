#include "struct.h"
#include "util.h"
#include <string.h>
#include <stdarg.h>
//#include <sys/stat.h>

char *make_message(const char *format, va_list list) // based on printf(3) man page
{
    int n;
    int size = 100;     // Guess we need no more than 100 bytes.
    char *p, *np;

    if ((p = (char*)malloc(size)) == NULL)
        return NULL;

    while (1) {

        // Try to print in the allocated space.
        n = vsnprintf(p, size, format, list);

        // If that worked, return the string.
        if (n > -1 && n < size)
            return p;

        // Else try again with more space.
        if (n > -1)    // glibc 2.1
            size = n+1;    // precisely what is needed
        else        // glibc 2.0
            size *= 2;    // twice the old size
        
        if ((np = (char*)realloc (p, size)) == NULL) {
            free(p);
            return NULL;
        } else {
            p = np; // QED
        }
    }
}

#define PRINT_FORMATED_AND_EXIT(...)    \
{    va_list list;                        \
        va_start(list, format );        \
        vfprintf(stderr, format, list);    \
        va_end(list );                    \
        fprintf(stderr, "\n");            \
        exit(1);                        \
}

void assert_message(bool assertion, const char *format, ...)
{
    if (!assertion)
        PRINT_FORMATED_AND_EXIT(format);
}

void *exit_message(const char *format, ...)
{
    PRINT_FORMATED_AND_EXIT(format);
	return NULL;
}

void null_check(const void *pointer) {
    if (!pointer)
        exit_message("null pointer");
}

const char* num_to_string(const struct number_string *ns, int num_items, int num)
{
    for (int i=0; i<num_items; i++) // reverse lookup nonterminal string
        if (num == ns[i].number)
            return ns[i].chars;
    exit_message("num not found");
    return NULL;
}

// file

#define INPUT_MAX_LEN    10000
#define ERROR_BIG        "Input file is too big"


long fsize(FILE* file) {
    if (!fseek(file, 0, SEEK_END)) {
        long size = ftell(file);
        if (size >= 0 && !fseek(file, 0, SEEK_SET))
            return size;
    }
    return -1;
}

struct byte_array *read_file(const struct byte_array *filename_ba)
{
    FILE * file;
    size_t read;
    uint8_t *str;
    long size;
    
    const char* filename_str = byte_array_to_string(filename_ba);

    if (!(file = fopen(filename_str, "rb")))
        exit_message(ERROR_FOPEN);
    if ((size = fsize(file)) < 0)
        exit_message(ERROR_FSIZE);
    else if (size > INPUT_MAX_LEN)
        exit_message(ERROR_BIG);
    if (!(str = malloc((size_t)size)))// + 1)))
        exit_message(ERROR_ALLOC);
    
    read = fread(str, 1, (size_t)size, file);
    if (feof(file) || ferror(file))
        exit_message(ERROR_FREAD);
    
    if (fclose(file))
        exit_message(ERROR_FCLOSE);
    
    struct byte_array* ba = byte_array_new_size(read);
    ba->data = str;
    byte_array_reset(ba);
    return ba;
}

int write_byte_array(struct byte_array* ba, FILE* file) {
    uint16_t len = ba->length;
    int n = fwrite(ba->data, 1, len, file);
    return len - n;
}

int write_file(const struct byte_array* filename, struct byte_array* bytes)
{
    const char *fname = byte_array_to_string(filename);
    FILE* file = fopen(fname, "w");
    if (!file) {
        DEBUGPRINT("could not open file %s\n", fname);
        return -1;
    }

    int r = fwrite(bytes->data, 1, bytes->length, file);
    DEBUGPRINT("\twrote %d bytes\n", r);
    int s = fclose(file);
    return (r<0) || s;
}

char* build_path(const char* dir, const char* name)
{
    int dirlen = dir ? strlen(dir) : 0;
    char* path = malloc(dirlen + 1 + strlen(name));
    const char* slash = (dir && dirlen && (dir[dirlen] != '/')) ? "/" : "";
    sprintf(path, "%s%s%s", dir ? dir : "", slash, name);
    return path;
}
