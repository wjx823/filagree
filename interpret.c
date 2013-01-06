//
//  interpret.c
//  filagree
//

#include "vm.h"
#include "compile.h"
#include "interpret.h"

#define FG_MAX_INPUT     256
#define ERROR_USAGE    "usage: filagree [file]"

void repl()
{
    char stdinput[FG_MAX_INPUT];
    struct context *context = context_new();

    for (;;) {
        fflush(stdin);
        stdinput[0] = 0;
        if (!fgets(stdinput, FG_MAX_INPUT, stdin)) {
            if (feof(stdin))
                return;
            if (ferror(stdin)) {
                printf("unknown error reading stdin\n");
                return;
            }
        }
        interpret_string(stdinput, context->find);
    }
}

void interpret_file(const struct byte_array *filename, find_c_var *find)
{
    struct byte_array *program = build_file(filename);
    execute(program, find);
}

void execute_file(const struct byte_array* filename, find_c_var *find)
{
    struct byte_array *program = read_file(filename);
    execute(program, find);
}

void run_file(const char* str, find_c_var *find, struct map *env)
{
    struct byte_array *filename = byte_array_from_string(str);
    struct byte_array *dotfgbc = byte_array_from_string(EXTENSION_BC);
    int fgbc = byte_array_find(filename, dotfgbc, 0);
    if (fgbc > 0) {
        execute_file(filename, find);
        return;
    }
    struct byte_array *dotfg = byte_array_from_string(EXTENSION_SRC);
    int fg = byte_array_find(filename, dotfg, 0);
    if (fg > 0)
        interpret_file(filename, find);
    else
        printf("invalid file name\n");
}

void interpret_string(const char *str, find_c_var *find)
{
    struct byte_array *input = byte_array_from_string(str);
    struct byte_array *program = build_string(input);
    execute(program, find);
}

#ifdef CLI
int main (int argc, char** argv)
{
    switch (argc) {
        case 1:     repl();                         break;
        case 2:     run_file(argv[1], NULL, NULL);  break;
        case 3:     compile_file(argv[1]);          break;
        default:    exit_message(ERROR_USAGE);      break;
    }
}
#endif // EXECUTABLE

