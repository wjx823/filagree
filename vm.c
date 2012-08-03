#include <stdio.h>
#include <signal.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>
#include "util.h"
#include "serial.h"
#include "vm.h"
#include "variable.h"
#include "sys.h"

#define VM_NAME "vm"

struct variable *run(struct Context *context, struct byte_array *program, bool in_context);
struct variable *rhs_pop(struct Context *context);
void display_code(struct Context *context, struct byte_array *code);


#ifdef DEBUG

#define VM_DEBUGPRINT(...) DEBUGPRINT(__VA_ARGS__ ); if (!context->runtime) return;

void print_operand_stack();

#define INDENT context->indent++;
#define UNDENT context->indent--;

#else // not DEBUG

#define INDENT
#define UNDENT

#define VM_DEBUGPRINT(...)

#endif // not DEBUG


// assertions //////////////////////////////////////////////////////////////

jmp_buf trying;

static void vm_exit() {
    longjmp(trying, 1);
}

void set_error(struct Context *context, const char *format, va_list list)
{
    if (!context)
        return;
    null_check(format);
    const char *message = make_message(format, list);
    context->error = variable_new_err(context, message);
}

void *vm_exit_message(struct Context *context, const char *format, ...)
{
    // make error variable
    va_list list;
    va_start(list, format);
    set_error(context, format, list);
    va_end(list);

    vm_exit();
    return NULL;
}

void vm_assert(struct Context *context, bool assertion, const char *format, ...)
{
    if (!assertion) {

        // make error variable
        va_list list;
        va_start(list, format);
        set_error(context, format, list);
        va_end(list);

        vm_exit();
    }
}

void vm_null_check(struct Context *context, const void* p) {
    vm_assert(context, p, "null pointer");
}

// state ///////////////////////////////////////////////////////////////////

struct program_state {
    //    struct byte_array *code;
    struct map *named_variables;
    struct array *all_variables;
    uint32_t pc;
};

struct program_state *program_state_new(struct Context *context)
{
    null_check(context);
    struct program_state *state = (struct program_state*)malloc(sizeof(struct program_state));
    state->named_variables = map_new();
    state->all_variables = array_new();
    stack_push(context->program_stack, state);
    return state;
}

struct Context *vm_init()
{
    struct Context *context = (struct Context*)malloc(sizeof(struct Context));
    null_check(context);
    context->program_stack = stack_new();
    context->operand_stack = stack_new();
    context->vm_exception = NULL;
    context->runtime = true;
    context->num_vars = 0;
	context->rhs = stack_new();
    context->args = array_new();

	struct variable *vm_var = func_map(context);
	context->vm_state = program_state_new(context);
	map_insert(context->vm_state->named_variables, byte_array_from_string(VM_NAME), vm_var);

    return context;
}

// garbage collection //////////////////////////////////////////////////////

void mark(struct Context *context, struct variable *root)
{
    null_check(context);
    if (root->map) {
        const struct array *values = map_values(root->map);
        for (int i=0; values && i<values->length; i++)
            mark(context, (struct variable*)array_get(values, i));
    }

    root->marked = true;
    switch (root->type) {
        case VAR_INT:
        case VAR_FLT:
        case VAR_STR:
        case VAR_FNC:
        case VAR_MAP:
            break;
        case VAR_LST:
            for (int i=0; i<root->list->length; i++)
                mark(context, (struct variable*)array_get(root->list, i));
            break;
        default:
            vm_exit_message(context, "bad var type");
            break;
    }
}

void sweep(struct Context *context, struct variable *root)
{
    null_check(context);
    struct program_state *state = (struct program_state*)stack_peek(context->program_stack, 0);
    struct array *vars = state->all_variables; 
    for (int i=0; i<vars->length; i++) {
        struct variable *v = (struct variable*)array_get(vars, i);
        if (!v->marked)
            variable_del(context, v);
        else
            v->marked = false;
    }
}

void garbage_collect(struct Context *context)
{
    null_check(context);
    struct program_state *state = (struct program_state*)stack_peek(context->program_stack, 0);
    struct array *vars = state->all_variables; 
    for (int i=0; i<vars->length; i++) {
        struct variable *v = (struct variable*)array_get(vars, i);
        mark(context, v);
        sweep(context, v);
    }
}

// display /////////////////////////////////////////////////////////////////

#ifdef DEBUG

const struct number_string opcodes[] = {
    {VM_NIL,    "NIL"},
    {VM_INT,    "INT"},
    {VM_BUL,    "BUL"},
    {VM_FLT,    "FLT"},
    {VM_STR,    "STR"},
    {VM_VAR,    "VAR"},
    {VM_FNC,    "FNC"},
    {VM_SRC,    "SRC"},
    {VM_LST,    "LST"},
    {VM_DST,    "DST"},
    {VM_MAP,    "MAP"},
    {VM_GET,    "GET"},
    {VM_PUT,    "PUT"},
    {VM_ADD,    "ADD"},
    {VM_SUB,    "SUB"},
    {VM_MUL,    "MUL"},
    {VM_DIV,    "DIV"},
    {VM_MOD,    "MOD"},
    {VM_AND,    "AND"},
    {VM_OR,     "ORR"},
    {VM_NOT,    "NOT"},
    {VM_NEG,    "NEG"},
    {VM_EQU,    "EQU"},
    {VM_NEQ,    "NEQ"},
    {VM_GTN,    "GTN"},
    {VM_LTN,    "LTN"},
    {VM_IFF,    "IFF"},
    {VM_JMP,    "JMP"},
    {VM_CAL,    "CAL"},
    {VM_MET,    "MET"},
    {VM_RET,    "RET"},
    {VM_ITR,    "ITR"},
    {VM_COM,    "COM"},
    {VM_TRY,    "TRY"},
};

void print_operand_stack(struct Context *context)
{
    null_check(context);
    struct variable *operand;
    for (int i=0; (operand = stack_peek(context->operand_stack, i)); i++)
        DEBUGPRINT("\t%s\n", variable_value_str(context, operand));
}

const char* indentation(struct Context *context)
{
    null_check(context);
    static char str[100];
    int tab = 0;
    while (tab < context->indent)
        str[tab++] = '\t';
    str[tab] = 0;
    return (const char*)str;
}

static void display_program_counter(struct Context *context, const struct byte_array *program)
{
    null_check(context);
    DEBUGPRINT("%s%2ld:%3d ", indentation(context), program->current-program->data, *program->current);
}

void display_code(struct Context *context, struct byte_array *code)
{
    null_check(context);
    bool was_running = context->runtime;
    context->runtime = false;

    INDENT
    run(context, code, false);
    UNDENT

    context->runtime = was_running;
}

void display_program(struct byte_array *program)
{
    struct Context *context = vm_init();

    INDENT
    DEBUGPRINT("%sprogram bytes:\n", indentation(context));

    INDENT
    for (int i=0; i<program->length; i++)
        DEBUGPRINT("%s%2d:%3d\n", indentation(context), i, program->data[i]);
    UNDENT

    DEBUGPRINT("%sprogram instructions:\n", indentation(context));
    byte_array_reset(program);
    struct byte_array* code = serial_decode_string(program);

    display_code(context, code);

    UNDENT
    UNDENT
}

#else // not DEBUG

void display_code(struct Context *context, struct byte_array *code) {}
const char* indentation(struct Context *context) { return ""; }

#endif // DEBUG

// instruction implementations /////////////////////////////////////////////

void src_size(struct Context *context, int32_t size)
{
    null_check(context);
    if (size > 1)
        while (stack_peek(context->rhs,1))
            stack_pop(context->rhs);
    else if (!stack_empty(context->rhs))
        return;

    while (size--)
        stack_push(context->rhs, variable_pop(context));
}

static void src(struct Context *context, enum Opcode op, struct byte_array *program)
{
    null_check(context);
    int32_t size = serial_decode_int(program);
    VM_DEBUGPRINT("%s %d\n", NUM_TO_STRING(opcodes, op), size);
    src_size(context, size);
}

void vm_call(struct Context *context)
{
    null_check(context);
    // get the function pointer from the stack
    struct variable *func = variable_pop(context);
    INDENT

    // call the function
    switch (func->type) {
        case VAR_FNC: {
            context->args = array_new();
            struct variable *v;
            for (int i=0; (v = (struct variable*)stack_peek(context->rhs, i)); i++)
                array_insert(context->args, context->args->length, v);
            run(context, func->str, false);
            } break;
        case VAR_C:
            func->cfnc(context);
            break;
        default:
            vm_exit_message(context, "not a function");
            break;
    }
    UNDENT
}

static inline void func_call(struct Context *context)
{
    null_check(context);
    VM_DEBUGPRINT("VM_CAL\n");
    vm_call(context);
}

static void push_list(struct Context *context, struct byte_array *program)
{
    null_check(context);
    int32_t num_items = serial_decode_int(program);
    DEBUGPRINT("LST %d", num_items);
    if (!context->runtime)
        VM_DEBUGPRINT("\n");
    struct array *items = array_new();
    struct map *map = map_new(); 

    while (num_items--) {
        struct variable* v = variable_pop(context);
        if (v->type == VAR_MAP)
            map_update(map, v->map); // mapped values are stored in the map, not list
        else
            array_insert(items, 0, v);
    }
    struct variable *list = variable_new_list(context, items);
    list->map = map;
    DEBUGPRINT(": %s\n", variable_value_str(context, list));
    variable_push(context, list);
}

static void push_map(struct Context *context, struct byte_array *program)
{
    null_check(context);
    int32_t num_items = serial_decode_int(program);
    DEBUGPRINT("MAP %d", num_items);
    if (!context->runtime)
        VM_DEBUGPRINT("\n");
    struct map *map = map_new();
    while (num_items--) {
        struct variable* value = variable_pop(context);
        struct variable* key = variable_pop(context);
        assert_message(key->type==VAR_STR, "non-string map index");
        map_insert(map, key->str, value);
    }
    struct variable *v = variable_new_map(context, map);
    DEBUGPRINT(": %s\n", variable_value_str(context, v));
    variable_push(context, v);
}

struct variable* variable_set(struct Context *context, struct variable *dst, const struct variable* src)
{
    null_check(context);
    vm_null_check(context, dst);
    vm_null_check(context, src);
    switch (src->type) {
        case VAR_NIL:                                           break;
        case VAR_BOOL:  dst->boolean = src->boolean;            break;
        case VAR_INT:   dst->integer = src->integer;            break;
        case VAR_FLT:   dst->floater = src->floater;            break;
        case VAR_FNC:
        case VAR_STR:   dst->str = byte_array_copy(src->str);   break;
        case VAR_MAP:   dst->map = src->map;                    break;
        case VAR_LST:
            dst->list = src->list;
            dst->list->current = src->list->data;               break;
        default:
            vm_exit_message(context, "bad var type");
            break;
    }
    if (src->type == VAR_STR)
        dst->str = byte_array_copy(src->str);
    dst->map = src->map;
    dst->type = src->type;
    return dst;
}

struct variable* variable_copy(struct Context *context, const struct variable* v)
{
    null_check(context);
    vm_null_check(context, v);
    struct variable *u = variable_new(context, (enum VarType)v->type);
    variable_set(context, u, v);
    return u;
}


// run /////////////////////////////////////////////////////////////////////

static struct variable *list_get_int(struct Context *context,
                                     const struct variable *indexable,
                                     const struct variable *index)
{
    null_check(context);
    null_check(indexable);
    null_check(index);

    uint32_t n = index->integer;
    enum VarType it = (enum VarType)indexable->type;
    switch (it) {
        case VAR_LST:
            return (struct variable*)array_get(indexable->list, n);
        case VAR_STR: {
            vm_assert(context, n<indexable->str->length, "index out of bounds");
            char *str = (char*)malloc(2);
            sprintf(str, "%c", indexable->str->data[n]);
            return variable_new_str(context, byte_array_from_string(str));
        }
        default:
            vm_exit_message(context, "indexing non-indexable");
            return NULL;
    }
}

void lookup(struct Context *context, struct variable *indexable, struct variable *index)
{
    null_check(context);
    if (!context->runtime)
        VM_DEBUGPRINT("GET\n");

    struct variable *item=0;

    switch (index->type) {
        case VAR_INT:
            item = list_get_int(context, indexable, index);
            break;
        case VAR_STR:
            if (indexable->map)
                item = (struct variable*)map_get(indexable->map, index->str);
            if (!item)
                item = builtin_method(context, indexable, index);
            assert_message(item, "did not find member");
            break;
        default:
            vm_exit_message(context, "bad index type");
            break;
    }
    DEBUGPRINT("%s\n", variable_value_str(context, item));
    variable_push(context, item);
}

static void list_get(struct Context *context)
{
    null_check(context);
    DEBUGPRINT("GET ");
    if (!context->runtime)
        VM_DEBUGPRINT("\n");
    struct variable *indexable, *index;
    indexable = variable_pop(context);
    index = variable_pop(context);
    lookup(context, indexable, index);
}

static void method(struct Context *context, struct byte_array *program)
{
    null_check(context);
    DEBUGPRINT("MET ");
    if (!context->runtime)
        VM_DEBUGPRINT("\n");
    struct variable *indexable, *index;
    indexable = variable_pop(context);
    index = variable_pop(context);
    lookup(context, indexable, index);
    stack_push(context->rhs, indexable);
    vm_call(context);
}

static int32_t jump(struct Context *context, struct byte_array *program)
{
    null_check(context);
    null_check(program);
    uint8_t *start = program->current;
    int32_t offset = serial_decode_int(program);
    DEBUGPRINT("JMP %d\n", offset);
    if (!context->runtime)
        return 0;

    if (offset < 0) // skip over current VM_JMP instruction when going backward
        offset -= (program->current - start) + 1;
    return offset;// - (program->current - start);
}

bool test_operand(struct Context *context)
{
    null_check(context);
    struct variable* v = variable_pop(context);
    bool indeed = false;
    switch (v->type) {
        case VAR_NIL:   indeed = false;                     break;
        case VAR_BOOL:  indeed = v->boolean;                break;
        case VAR_INT:   indeed = v->integer;                break;
        default:
            vm_exit_message(context, "bad iff operand");
            break;
    }
    return indeed;
}

static int32_t iff(struct Context *context, struct byte_array *program)
{
    null_check(context);
    null_check(program);
    int32_t offset = serial_decode_int(program);
    DEBUGPRINT("IF %d\n", offset);
    if (!context->runtime)
        return 0;
    return test_operand(context) ? 0 : (VOID_INT)offset;
}

static void push_nil(struct Context *context)
{
    null_check(context);
    struct variable* var = variable_new_nil(context);
    VM_DEBUGPRINT("NIL\n");
    variable_push(context, var);
}

static void push_int(struct Context *context, struct byte_array *program)
{
    null_check(context);
    null_check(program);
    int32_t num = serial_decode_int(program);
    VM_DEBUGPRINT("INT %d\n", num);
    struct variable* var = variable_new_int(context, num);
    variable_push(context, var);
}

static void push_bool(struct Context *context, struct byte_array *program)
{
    null_check(context);
    null_check(program);
    int32_t num = serial_decode_int(program);
    VM_DEBUGPRINT("BOOL %d\n", num);
    struct variable* var = variable_new_bool(context, num);
    variable_push(context, var);
}

static void push_float(struct Context *context, struct byte_array *program)
{
    null_check(context);
    null_check(program);
    float num = serial_decode_float(program);
    VM_DEBUGPRINT("FLT %f\n", num);
    struct variable* var = variable_new_float(context, num);
    variable_push(context, var);
}

struct variable *find_var(struct Context *context, const struct byte_array *name)
{
    null_check(context);
    null_check(name);
    const struct program_state *state = (const struct program_state*)stack_peek(context->program_stack, 0);
    struct map *var_map = state->named_variables;
    struct variable *v = (struct variable*)map_get(var_map, name);
    //DEBUGPRINT("find_var(%s) in %p,%p = %p\n", byte_array_to_string(name), state, var_map, v);
    if (!v)
        v = (struct variable*)map_get(context->vm_state->named_variables, name);
    return v;
}

static void push_var(struct Context *context, struct byte_array *program)
{
    struct byte_array* name = serial_decode_string(program);
    VM_DEBUGPRINT("VAR %s\n", byte_array_to_string(name));
    struct variable *v = find_var(context, name);
    vm_assert(context, v, "variable %s not found", byte_array_to_string(name));
    variable_push(context, v);
}

static void push_str(struct Context *context, struct byte_array *program)
{
    struct byte_array* str = serial_decode_string(program);
    VM_DEBUGPRINT("STR '%s'\n", byte_array_to_string(str));
    struct variable* v = variable_new_str(context, str);
    variable_push(context, v);
}

static void push_fnc(struct Context *context, struct byte_array *program)
{
    uint32_t fcodelen = serial_decode_int(program);
    struct byte_array* fbody = byte_array_new_size(fcodelen);
    memcpy(fbody->data, program->current, fcodelen);

    DEBUGPRINT("FNC %u\n", fcodelen);
    display_code(context, fbody);

    if (context->runtime) {
        struct variable* var = variable_new_fnc(context, (struct byte_array*)fbody);
        variable_push(context, var);
    }

    program->current += fcodelen;
}

void set_named_variable(struct Context *context, 
                        struct program_state *state,
                        const struct byte_array *name,
                        const struct variable *value)
{
    if (!state)
        state = (struct program_state*)stack_peek(context->program_stack, 0);
    struct map *var_map = state->named_variables;
    struct variable *to_var = find_var(context, name);

    if (!to_var) { // new variable
        to_var = variable_copy(context, value);
        to_var->name = byte_array_copy(name);
    } else
        variable_set(context, to_var, value);

    map_insert(var_map, name, to_var);

    //DEBUGPRINT(" (SET %s to %s in {%p,%p,%p})\n", byte_array_to_string(name), variable_value(to_var), state, var_map, to_var);
}

struct variable *rhs_pop(struct Context *context)
{
    struct variable *value = (struct variable*)stack_pop(context->rhs);
    if (!value)
        value = variable_new_nil(context);
    return value;
}

static void set(struct Context *context, struct program_state *state, struct byte_array *program)
{
    null_check(context);
    struct byte_array *name = serial_decode_string(program);    // destination variable name
    if (!context->runtime)
        VM_DEBUGPRINT("SET %s\n", byte_array_to_string(name));

    struct variable *value = rhs_pop(context);
    DEBUGPRINT("SET %s to %s\n", byte_array_to_string(name), variable_value_str(context, value));
    set_named_variable(context, state, name, value); // set the variable to the value
}

static void dst(struct Context *context) // drop unused assignment right-hand-side values
{
    VM_DEBUGPRINT("DST\n");
    context->rhs = stack_new();
}

static void list_put(struct Context *context)
{
    DEBUGPRINT("PUT");
    if (!context->runtime)
        VM_DEBUGPRINT("\n");
    struct variable* recipient = variable_pop(context);
    struct variable* key = variable_pop(context);
    struct variable *value = rhs_pop(context);

    switch (key->type) {
        case VAR_INT:
            switch (recipient->type) {
                case VAR_LST:
                    array_set(recipient->list, key->integer, value);
                    break;
                case VAR_STR:
                    ((uint8_t*)recipient->str)[key->integer] = (uint8_t)value->integer;
                    break;
                default:
                    vm_exit_message(context, "indexing non-indexable");
            } break;
        case VAR_STR:
            if (!recipient->map)
                recipient->map = map_new();
            map_insert(recipient->map, key->str, value);
            break;
        default:
            vm_exit_message(context, "bad index type");
            break;
    }
    DEBUGPRINT(": %s\n", variable_value_str(context, recipient));
}

static struct variable *binary_op_int(struct Context *context,
                                      enum Opcode op,
                                      const struct variable *u,
                                      const struct variable *v)
{
    int32_t m = u->integer;
    int32_t n = v->integer;
    int32_t i;
    switch (op) {
        case VM_MUL:    i = m * n;    break;
        case VM_DIV:    i = m / n;    break;
        case VM_ADD:    i = m + n;    break;
        case VM_SUB:    i = m - n;    break;
        case VM_AND:    i = m && n;   break;
        case VM_EQU:    i = m == n;   break;
        case VM_OR:     i = m || n;   break;
        case VM_GTN:    i = m > n;    break;
        case VM_LTN:    i = m < n;    break;
        case VM_BND:    i = m & n;    break;
        case VM_BOR:    i = m | n;    break;
        case VM_MOD:    i = m % n;    break;
        case VM_XOR:    i = m ^ n;    break;
        case VM_RSF:    i = m >> n;   break;
        case VM_LSF:    i = m << n;   break;

        default:
            return (struct variable*)vm_exit_message(context, "bad math int operator");
    }
    return variable_new_int(context, i);
}

static struct variable *binary_op_float(struct Context *context,
                                        enum Opcode op,
                                        const struct variable *u,
                                        const struct variable *v)
{
    float m = u->floater;
    float n = v->floater;
    float f = 0;
    switch (op) {
        case VM_MUL:    f = m * n;                            break;
        case VM_DIV:    f = m / n;                            break;
        case VM_ADD:    f = m + n;                            break;
        case VM_SUB:    f = m - n;                            break;
        case VM_NEQ:    f = m != n;                            break;
        case VM_GTN:    return variable_new_int(context, n > m);
        case VM_LTN:    return variable_new_int(context, n < m);
        default:
            return (struct variable*)vm_exit_message(context, "bad math float operator");
    }
    return variable_new_float(context, f);
}

static bool is_num(enum VarType vt) {
    return vt == VAR_INT || vt == VAR_FLT;
}

static struct variable *binary_op_str(struct Context *context,
                                      enum Opcode op,
                                      const struct variable *u,
                                      const struct variable *v)
{
    null_check(context);
    struct variable *w = NULL;
    struct byte_array *ustr = variable_value(context, u);
    struct byte_array *vstr = variable_value(context, v);

    switch (op) {
        case VM_ADD:    w = variable_new_str(context, byte_array_concatenate(2, vstr, ustr));    break;
        case VM_EQU:    w = variable_new_int(context, byte_array_equals(ustr, vstr));            break;
        default:
            return (struct variable*)vm_exit_message(context, "unknown string operation");
    }
    return w;
}

static bool variable_compare(struct Context *context,
                             const struct variable *u,
                             const struct variable *v)
{
    null_check(context);
    if (!u != !v)
        return false;
    enum VarType ut = (enum VarType)u->type;
    enum VarType vt = (enum VarType)v->type;

    if (ut != vt)
        return false;

    switch (ut) {
        case VAR_LST:
            if (u->list->length != v->list->length)
                return false;
            for (int i=0; i<u->list->length; i++) {
                struct variable *ui = (struct variable*)array_get(u->list, i);
                struct variable *vi = (struct variable*)array_get(v->list, i);
                if (!variable_compare(context, ui, vi))
                    return false;
            }
            // for list, check the map too
        case VAR_MAP: {
            struct array *keys = map_keys(u->map);
            for (int i=0; i<keys->length; i++) {
                struct byte_array *key = (struct byte_array*)array_get(keys, i);
                struct variable *uvalue = (struct variable*)map_get(u->map, key);
                struct variable *vvalue = (struct variable*)map_get(v->map, key);
                if (!variable_compare(context, uvalue, vvalue))
                    return false;
            }
            return true; }
        case VAR_INT:
            return u->integer == v->integer;
        case VAR_FLT:
            return u->floater == v->floater;
        case VAR_STR:
            return byte_array_equals(u->str, v->str);
        default:
            return (bool)vm_exit_message(context, "bad comparison");
    }
}

static struct variable *binary_op_lst(struct Context *context,
                                      enum Opcode op,
                                      const struct variable *u,
                                      const struct variable *v)
{
    null_check(context);
    vm_assert(context, u->type==VAR_LST && v->type==VAR_LST, "list op with non-lists");
    struct variable *w = NULL;

    switch (op) {
        case VM_ADD:
            w = variable_copy(context, v);
            for (int i=0; i<u->list->length; i++)
                array_add(w->list, array_get(u->list, i));
            map_update(w->map, u->map);
            break;
        default:
            return (struct variable*)vm_exit_message(context, "unknown string operation");
    }

    return w;
}

static struct variable *binary_op_nil(struct Context *context,
                                      enum Opcode op,
                                      const struct variable *u,
                                      const struct variable *v)
{
    null_check(context);
    vm_assert(context, u->type==VAR_NIL || v->type==VAR_NIL, "nil op with non-nils");
    if (v->type == VAR_NIL) {
        if (u->type == VAR_NIL)
            return variable_new_nil(context);
        return binary_op_nil(context, op, v, u); // 1st var should be nil
    }

    switch (op) {
        case VM_OR:     return variable_copy(context, v);
        case VM_AND:    return variable_new_nil(context);
        default:
            return vm_exit_message(context, "unknown binary nil op");
    }
}

static void binary_op(struct Context *context, enum Opcode op)
{
    null_check(context);
    if (!context->runtime)
        VM_DEBUGPRINT("%s\n", NUM_TO_STRING(opcodes, op));

    const struct variable *u = variable_pop(context);
    const struct variable *v = variable_pop(context);
    enum VarType ut = (enum VarType)u->type;
    enum VarType vt = (enum VarType)v->type;
    struct variable *w;

    if (ut == VAR_NIL || vt == VAR_NIL) {
        w = binary_op_nil(context, op, u, v);
    } else if (op == VM_EQU) {
        bool same = variable_compare(context, u, v);
        w = variable_new_int(context, same);
    } else {
        bool floater  = (ut == VAR_FLT && is_num(vt)) || (vt == VAR_FLT && is_num(ut));

        if (vt == VAR_STR || ut == VAR_STR)         w = binary_op_str(context, op, u, v);
        else if (floater)                           w = binary_op_float(context, op, u, v);
        else if (ut == VAR_INT && vt == VAR_INT)    w = binary_op_int(context, op, v, u);
        else if (vt == VAR_LST)                     w = binary_op_lst(context, op, u, v);
        else
            vm_exit_message(context, "unknown binary op");
    }

    variable_push(context, w);

    DEBUGPRINT("%s(%s,%s) = %s\n",
               NUM_TO_STRING(opcodes, op),
               variable_value_str(context, v),
               variable_value_str(context, u),
               variable_value_str(context, w));
}

static void unary_op(struct Context *context, enum Opcode op)
{
    null_check(context);
    if (!context->runtime)
        VM_DEBUGPRINT("%s\n", NUM_TO_STRING(opcodes, op));

    struct variable *v = (struct variable*)variable_pop(context);
    struct variable *result = NULL;

    switch (v->type) {
        case VAR_NIL:
        {
            switch (op) {
                case VM_NEG:    result = variable_new_nil(context);              break;
                case VM_NOT:    result = variable_new_bool(context, true);       break;
                default:        vm_exit_message(context, "bad math operator");   break;
            }
        } break;
        case VAR_INT: {
            int32_t n = v->integer;
            switch (op) {
                case VM_NEG:    result = variable_new_int(context, -n);          break;
                case VM_NOT:    result = variable_new_bool(context, !n);         break;
                case VM_INV:    result = variable_new_int(context, ~n);          break;
                default:        vm_exit_message(context, "bad math operator");   break;
            }
        } break;
        default:
            if (op == VM_NOT)
                result = variable_new_bool(context, false);
            else
                vm_exit_message(context, "bad math type");
            break;
    }

    variable_push(context, result);

    DEBUGPRINT("%s(%s) = %s\n",
               NUM_TO_STRING(opcodes, op),
               variable_value_str(context, v),
               variable_value_str(context, result));
}

// FOR who IN what WHERE where DO how
static void iterate(struct Context *context, 
                    enum Opcode op,
                    struct program_state *state,
                    struct byte_array *program)
{
    null_check(context);
    struct byte_array *who = serial_decode_string(program);
    struct byte_array *where = serial_decode_string(program);
    struct byte_array *how = serial_decode_string(program);

#ifdef DEBUG
    DEBUGPRINT("%s %s\n",
               NUM_TO_STRING(opcodes, op),
               byte_array_to_string(who));
    if (!context->runtime) {
        if (where && where->length) {
            DEBUGPRINT("%s\tWHERE\n", indentation(context));
            display_code(context, where);
        }
        DEBUGPRINT("%s\tDO\n", indentation(context));
        display_code(context, how);
        return;
    }
#endif

    bool comprehending = (op == VM_COM);
    struct variable *result = comprehending ? variable_new_list(context, NULL) : NULL;

    struct variable *what = variable_pop(context);
    for (int i=0; i<what->list->length; i++) {

        struct variable *that = (struct variable*)array_get(what->list, i);
        set_named_variable(context, state, who, that);

        byte_array_reset(where);
        byte_array_reset(how);
        run(context, where, true);
        if (!where || !where->length || test_operand(context)) {

            run(context, how, true);

            if (comprehending) {
                struct variable *item = (struct variable*)stack_pop(context->operand_stack);
                array_add(result->list, item);
            }
        }
    }

    if (comprehending)
        stack_push(context->operand_stack, result);
}

static inline void build_arg_list(struct Context *context)
{
    struct variable *result = variable_new_list(context, context->args);
    stack_push(context->operand_stack, result);
}

static inline void vm_trycatch(struct Context *context, struct byte_array *program)
{
    struct byte_array *trial = serial_decode_string(program);
    DEBUGPRINT("TRY %d\n", trial->length);
    display_code(context, trial);
    struct byte_array *name = serial_decode_string(program);
    struct byte_array *catcher = serial_decode_string(program);
    DEBUGPRINT("%sCATCH %s %d\n", indentation(context), byte_array_to_string(name), catcher->length);
    display_code(context, catcher);
    if (!context->runtime)
        return;

    run(context, trial, true);
    if (context->vm_exception) {
        set_named_variable(context, NULL, name, context->vm_exception);
        context->vm_exception = NULL;
        run(context, catcher, true);
    }
}

struct variable *run(struct Context *context, struct byte_array *program, bool in_context)
{
    null_check(context);
    struct program_state *state = NULL;
    program->current = program->data;
    if (context->runtime) {
        if (in_context)
            state = (struct program_state*)stack_peek(context->program_stack, 0);
        else
            state = program_state_new(context);
    }
    //DEBUGPRINT("run %d %d %p\n", runtime, context, state);
    //DEBUGPRINT("\t%p < %p + %d? %s\n", program->current, program->data, program->length, program->current < program->data + program->length ? "yes":"no");

    while (program->current < program->data + program->length) {
        enum Opcode inst = (enum Opcode)*program->current;
#ifdef DEBUG
        display_program_counter(context, program);
#endif
        program->current++; // increment past the instruction

        if (inst == VM_RET) {
            src(context, inst, program);
            break;
        } else if (inst == VM_TRO) {
            DEBUGPRINT("THROW\n");
            if (context->runtime) {
                context->vm_exception = (struct variable*)stack_pop(context->operand_stack);
                break;
            } else
                continue;
        }

        int32_t pc_offset = 0;

        switch (inst) {
            case VM_MUL:
            case VM_DIV:
            case VM_ADD:
            case VM_SUB:
            case VM_AND:
            case VM_EQU:
            case VM_NEQ:
            case VM_GTN:
            case VM_LTN:
            case VM_BND:
            case VM_BOR:
            case VM_MOD:
            case VM_XOR:
            case VM_INV:
            case VM_RSF:
            case VM_LSF:
            case VM_OR:     binary_op(context, inst);               break;
            case VM_NEG:
            case VM_NOT:    unary_op(context, inst);                break;
            case VM_SRC:    src(context, inst, program);            break;
            case VM_DST:    dst(context);                           break;
            case VM_SET:    set(context, state, program);           break;
            case VM_JMP:    pc_offset = jump(context, program);     break;
            case VM_IFF:    pc_offset = iff(context, program);      break;
            case VM_CAL:    func_call(context);                     break;
            case VM_LST:    push_list(context, program);            break;
            case VM_MAP:    push_map(context, program);             break;
            case VM_GET:    list_get(context);                      break;
            case VM_PUT:    list_put(context);                      break;
            case VM_NIL:    push_nil(context);                      break;
            case VM_INT:    push_int(context, program);             break;
            case VM_FLT:    push_float(context, program);           break;
            case VM_BUL:    push_bool(context, program);            break;
            case VM_STR:    push_str(context, program);             break;
            case VM_VAR:    push_var(context, program);             break;
            case VM_FNC:    push_fnc(context, program);             break;
            case VM_MET:    method(context, program);               break;
            case VM_COM:
            case VM_ITR:    iterate(context, inst, state, program); break;
            case VM_TRY:    vm_trycatch(context, program);          break;
            default:
                return (struct variable*)vm_exit_message(context, ERROR_OPCODE);
        }
        program->current += pc_offset;
    }

    //DEBUGPRINT("run done %d %d %p\n", runtime, context, state);
    if (!context->runtime)
        return NULL;
    if (!in_context)
        stack_pop(context->program_stack);
    return (struct variable*)stack_peek(context->operand_stack, 0);
}

struct variable *execute(struct byte_array *program,
                         bool in_context,
                         bridge *callback_to_c)
{
    null_check(program);
    DEBUGPRINT("execute:\n");
    struct Context *context = vm_init();
    context->callback2c = callback_to_c;
    vm_assert(context, program!=0 && program->data!=0, ERROR_NULL);
    byte_array_reset(program);
    struct byte_array* code = serial_decode_string(program);

#ifdef DEBUG
    context->indent = 1;
#endif

    struct variable *v;
    if (!setjmp(trying))
        v = run(context, code, in_context);
    else
        v = context->error;

    return v;
}
