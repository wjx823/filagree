#include "struct.h"
#include "util.h"
#include <string.h>
#include <stdarg.h>

#ifdef ANDROID
#include <android/log.h>
#define TAG "filagree"
#endif

#ifdef MBED

#include "mbed.h"

static Serial usbTxRx(USBTX, USBRX);

size_t strnlen(char *s, size_t maxlen)
{
	size_t i;
	for (i= 0; i<maxlen && *s; i++, s++);
	return i;
}

char *strnstr(const char *s, const char *find, size_t slen)
{
	char c, sc;
	size_t len;
    
	if ((c = *find++) != '\0') {
		len = strlen(find);
		do {
			do {
				if (slen-- < 1 || (sc = *s++) == '\0')
					return (NULL);
			} while (sc != c);
			if (len > slen)
				return (NULL);
		} while (strncmp(s, find, len) != 0);
		s--;
	}
	return ((char *)s);
}

#endif // MBED


#define MESSAGE_MAX 100

void log_print(const char *format, ...)
{
    static char log_message[MESSAGE_MAX+1] = "";
    char one_line[MESSAGE_MAX];

    char *newline;
    va_list list;
    va_start(list, format);
    const char *message = make_message(format, list);
    va_end(list);
    size_t log_len = strnlen(log_message, MESSAGE_MAX);
    strncat(log_message, message, MESSAGE_MAX - log_len);
    log_len = strnlen(log_message, MESSAGE_MAX);
    if (log_len == MESSAGE_MAX)
        log_message[MESSAGE_MAX-1] = '\n';
    if (!(newline = strnstr(log_message, "\n", MESSAGE_MAX)))
        return;
    size_t line_len = newline - log_message;
    memcpy(one_line, log_message, line_len);
    one_line[line_len] = 0;

#ifdef ANDROID
    __android_log_write(ANDROID_LOG_ERROR, TAG, one_line);
#elif defined IOS
    NSLog(@"%s", one_line);
#elifdef MBED
    usbTxRx.printf("%s\n", one_line);    
#else
    printf("%s\n", one_line);    
#endif

    memmove(log_message, newline+1, log_len-line_len);
}

const char *make_message(const char *format, va_list ap)
{
    static char message[MESSAGE_MAX];
    vsnprintf(message, MESSAGE_MAX, format, ap);
    return message;
}

void exit_message2(const char *format, va_list list)
{
    const char *message = make_message(format, list);
    log_print("\n%s\n", message);
    va_end(list);
    exit(1);
}

void assert_message(bool assertion, const char *format, ...)
{
    if (assertion)
        return;
    va_list list;
    va_start(list, format);
    exit_message2(format, list);
}

void *exit_message(const char *format, ...)
{
    va_list list;
    va_start(list, format);
    exit_message2(format, list);
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

#define INPUT_MAX_LEN    100000
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
    if (!(str = (uint8_t*)malloc((size_t)size)))// + 1)))
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
    char* path = (char*)malloc(dirlen + 1 + strlen(name));
    const char* slash = (dir && dirlen && (dir[dirlen] != '/')) ? "/" : "";
    sprintf(path, "%s%s%s", dir ? dir : "", slash, name);
    return path;
}
