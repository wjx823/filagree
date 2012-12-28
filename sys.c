#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include "hal.h"
#include "interpret.h"
#include "serial.h"
#include "struct.h"
#include "sys.h"
#include "variable.h"
#include "vm.h"
#include "ui.h"
#include "util.h"

#define RESERVED_SYS  "sys"

struct string_func
{
    const char* name;
    callback2func* func;
};

struct variable *sys = NULL;


// system functions

struct variable *sys_print(struct context *context)
{
    null_check(context);
    struct variable *args = (struct variable*)stack_pop(context->operand_stack);
    assert_message(args && args->type==VAR_SRC && args->list, "bad print arg");
    for (int i=1; i<args->list->length; i++) {
        struct variable *arg = (struct variable*)array_get(args->list, i);
        DEBUGPRINT("%s\n", variable_value_str(context, arg));
    }
    return NULL;
}

struct variable *sys_save(struct context *context)
{
    struct variable *value = (struct variable*)stack_pop(context->operand_stack);
    struct variable *v = (struct variable*)array_get(value->list, 1);
    struct variable *path = (struct variable*)array_get(value->list, 2);
    variable_save(context, v, path);
    return NULL;
}

struct variable *sys_load(struct context *context)
{
    struct variable *value = (struct variable*)stack_pop(context->operand_stack);
    struct variable *path = (struct variable*)array_get(value->list, 1);
    return variable_load(context, path);
}

struct variable *sys_read(struct context *context)
{
    struct variable *value = (struct variable*)stack_pop(context->operand_stack);
    struct variable *path = (struct variable*)array_get(value->list, 1);
    struct byte_array *bytes = read_file(path->str);
    if (bytes)
        return variable_new_str(context, bytes);
    struct byte_array *str = byte_array_from_string("could not load file");
    context->vm_exception = variable_new_str(context, str);
    return NULL;
}

struct variable *sys_run(struct context *context)
{
    struct variable *value = (struct variable*)stack_pop(context->operand_stack);
    struct variable *script = (struct variable*)array_get(value->list, 1);
    execute(script->str, false, NULL);
    return NULL;
}

struct variable *sys_interpret(struct context *context)
{
    stack_pop(context->operand_stack); // self
    struct variable *script = (struct variable*)stack_pop(context->operand_stack);
    char *str = byte_array_to_string(script->str);
    interpret_string(str, NULL);
    return NULL;
}

struct variable *sys_rm(struct context *context)
{
    struct variable *value = (struct variable*)stack_pop(context->operand_stack);
    struct variable *path = (struct variable*)array_get(value->list, 1);
    remove(byte_array_to_string(path->str));
    return NULL;
}

struct variable *sys_window(struct context *context)
{
    stack_pop(context->operand_stack); // self
    hal_window();
    return NULL;
}

struct variable *sys_loop(struct context *context)
{
    stack_pop(context->operand_stack); // self
    hal_loop();
    return NULL;
}

struct variable *sys_args(struct context *context)
{
    stack_pop(context->operand_stack); // self
    struct program_state *above = (struct program_state*)stack_peek(context->program_stack, 1);
    return variable_new_list(context, above->args);
}

struct variable *sys_button(struct context *context)
{
    stack_pop(context->operand_stack); // self
    int32_t x = ((struct variable*)stack_pop(context->operand_stack))->integer;
    int32_t y = ((struct variable*)stack_pop(context->operand_stack))->integer;
    int32_t w = ((struct variable*)stack_pop(context->operand_stack))->integer;
    int32_t h = ((struct variable*)stack_pop(context->operand_stack))->integer;
    const char *str = byte_array_to_string(((struct variable*)stack_pop(context->operand_stack))->str);
    hal_button(x, y, w, h, str, NULL, NULL);
    return NULL;
}

struct variable *sys_atoi(struct context *context)
{
    struct variable *value = (struct variable*)stack_pop(context->operand_stack);
    char *str = (char*)((struct variable*)array_get(value->list, 1))->str->data;
    uint32_t offset = value->list->length > 2 ? ((struct variable*)array_get(value->list, 2))->integer : 0;

    int n=0, i=0;
    bool negative = false;
    if (str[offset] == '-') {
        negative = true;
        i++;
    };

    while (isdigit(str[offset+i]))
        n = n*10 + str[offset + i++] - '0';

    variable_push(context, variable_new_int(context, n));
    variable_push(context, variable_new_int(context, i));
    return variable_new_src(context, 2);
}

struct variable *sys_input(struct context *context)
{
    stack_pop(context->operand_stack); // self
    int32_t x = ((struct variable*)stack_pop(context->operand_stack))->integer;
    int32_t y = ((struct variable*)stack_pop(context->operand_stack))->integer;
    int32_t w = ((struct variable*)stack_pop(context->operand_stack))->integer;
    int32_t h = ((struct variable*)stack_pop(context->operand_stack))->integer;
    const char *str = byte_array_to_string(((struct variable*)stack_pop(context->operand_stack))->str);
    hal_input(x, y, w, h, str, false);
    return NULL;
}

struct string_func builtin_funcs[] = {
	{"args",        &sys_args},
    {"print",       &sys_print},
    {"read",        &sys_read},
    {"save",        &sys_save},
    {"load",        &sys_load},
    {"run",         &sys_run},
    {"interpret",   &sys_interpret},
    {"remove",      &sys_rm},
    {"window",      &sys_window},
    {"loop",        &sys_loop},
    {"button",      &sys_button},
    {"input",       &sys_input},
    {"atoi",        &sys_atoi},
};

struct variable *sys_find(struct context *context, const struct byte_array *name)
{
    if (strncmp(RESERVED_SYS, (const char*)name->data, strlen(RESERVED_SYS)))
        return NULL;
    if (!sys) { // create sys if needed
        struct map *sys_func_map = map_new(NULL, NULL);
        for (int i=0; i<ARRAY_LEN(builtin_funcs); i++) {
            struct byte_array *name = byte_array_from_string(builtin_funcs[i].name);
            struct variable *value = variable_new_c(context, builtin_funcs[i].func);
            map_insert(sys_func_map, name, value);
        }
        sys = variable_new_map(context, sys_func_map);
    }
    return sys;
}

// built-in member functions

#define FNC_STRING      "string"
#define FNC_LIST        "list"
#define FNC_TYPE        "type"
#define FNC_LENGTH      "length"
#define FNC_CHAR        "char"
#define FNC_HAS         "has"
#define FNC_KEYS        "keys"
#define FNC_VALUES      "values"
#define FNC_SERIALIZE   "serialize"
#define FNC_DESERIALIZE "deserialize"
#define FNC_SORT        "sort"
#define FNC_FIND        "find"
#define FNC_REPLACE     "replace"
#define FNC_PART        "part"
#define FNC_REMOVE      "remove"
#define FNC_INSERT      "insert"
//#define FNC_ADD         "add"


int compar(struct context *context, const void *a, const void *b, struct variable *comparator)
{
    struct variable *av = *(struct variable**)a;
    struct variable *bv = *(struct variable**)b;

    if (comparator) {

        byte_array_reset(comparator->str);
        vm_call(context, comparator, av, bv, NULL);

        struct variable *result = (struct variable*)stack_pop(context->operand_stack);
        if (result->type == VAR_SRC)
            result = array_get(result->list, 0);
        assert_message(result->type == VAR_INT, "non-integer comparison result");
        return result->integer;

    } else {

        enum VarType at = av->type;
        enum VarType bt = bv->type;

        if (at == VAR_INT && bt == VAR_INT) {
            // DEBUGPRINT("compare %p:%d to %p:%d : %d\n", av, av->integer, bv, bv->integer, av->integer - bv->integer);
            return av->integer - bv->integer;
        } else
            DEBUGPRINT("can't compare %s to %s\n", var_type_str(at), var_type_str(bt));

        vm_exit_message(context, "incompatible types for comparison");
        return 0;
    }
}

void heapset(size_t width, void *base0, uint32_t index0, void *base1, uint32_t index1) {
    uint8_t *p0 = (uint8_t*)base0 + index0 * width;
    uint8_t *p1 = (uint8_t*)base1 + index1 * width;
    while (width--)
        *(p0 + width) = *(p1 + width);
}

int heapcmp(struct context *context,
            size_t width, void *base0, uint32_t index0, void *base1, uint32_t index1,
            struct variable *comparator)
{
    uint8_t *p0 = (uint8_t*)base0 + index0 * width;
    uint8_t *p1 = (uint8_t*)base1 + index1 * width;
    return compar(context, p0, p1, comparator);
}

int heapsortfg(struct context *context, void *base, size_t nel, size_t width, struct variable *comparator)
{
    if (!nel)
        return 0;
    void *t = malloc(width); // the temporary value
    unsigned int n = nel, parent = nel/2, index, child; // heap indexes
    for (;;) { // loop until array is sorted
        if (parent > 0) { // first stage - Sorting the heap
            heapset(width, t, 0, base, --parent);
        } else { // second stage - Extracting elements in-place
            if (!--n) { // make the heap smaller
                free(t);
                return 0; // When the heap is empty, we are done
            }
            heapset(width, t, 0, base, n);
            heapset(width, base, n, base, 0);
        }
        // insert operation - pushing t down the heap to replace the parent
        index = parent; // start at the parent index
        child = index * 2 + 1; // get its left child index
        while (child < n) {
            if (child + 1 < n  && // choose the largest child
                heapcmp(context, width, base, child+1, base, child, comparator) > 0) {
                child++; // right child exists and is bigger
            }
            // is the largest child larger than the entry?
            if (heapcmp(context, width, base, child, t, 0, comparator) > 0) {
                heapset(width, base, index, base, child);
                index = child; // move index to the child
                child = index * 2 + 1; // get the left child and go around again
            } else
                break; // t's place is found
        }
        // store the temporary value at its new location
        heapset(width, base, index, t, 0);
    }
}

struct variable *cfnc_char(struct context *context)
{
    struct variable *args = (struct variable*)stack_pop(context->operand_stack);
    struct variable *from = (struct variable*)array_get(args->list, 0);
    struct variable *index = (struct variable*)array_get(args->list, 1);

    assert_message(from->type == VAR_STR, "char from a non-str");
    assert_message(index->type == VAR_INT, "non-int index");
    uint8_t n = byte_array_get(from->str, index->integer);
    return variable_new_int(context, n);
}

struct variable *cfnc_sort(struct context *context)
{
    struct variable *args = (struct variable*)stack_pop(context->operand_stack);
    struct variable *self = (struct variable*)array_get(args->list, 0);

    assert_message(self->type == VAR_LST, "sorting a non-list");
    struct variable *comparator = (args->list->length > 1) ?
        (struct variable*)array_get(args->list, 1) :
        NULL;

    int num_items = self->list->length;

    int success = heapsortfg(context, self->list->data, num_items, sizeof(struct variable*), comparator);
    assert_message(!success, "error sorting");
    return NULL;
}

struct variable *cfnc_chop(struct context *context, bool part)
{
    struct variable *args = (struct variable*)stack_pop(context->operand_stack);
    struct variable *self = (struct variable*)array_get(args->list, 0);
    struct variable *start = (struct variable*)array_get(args->list, 1);

    assert_message(start->type == VAR_INT, "non-integer index");
    int32_t beginning = start->integer;

    int32_t foraslongas;
    if (args->list->length > 2) {
        struct variable *length = (struct variable*)array_get(args->list, 2);
        assert_message(length->type == VAR_INT, "non-integer length");
        foraslongas = length->integer;
    } else
        foraslongas = part ? self->str->length - beginning : 1;

    struct variable *result = variable_copy(context, self);
    if (part)
        result = variable_part(context, result, beginning, foraslongas);
    else
        variable_remove(result, beginning, foraslongas);
    return result;
}

static inline struct variable *cfnc_part(struct context *context) {
    return cfnc_chop(context, true);
}

static inline struct variable *cfnc_remove(struct context *context) {
    return cfnc_chop(context, false);
}

struct variable *cfnc_find2(struct context *context, bool has)
{
    struct variable *args = (struct variable*)stack_pop(context->operand_stack);
    struct variable *self = (struct variable*)array_get(args->list, 0);
    struct variable *sought = (struct variable*)array_get(args->list, 1);
    struct variable *start = args->list->length > 2 ? (struct variable*)array_get(args->list, 2) : NULL;
    null_check(self);
    null_check(sought);

    struct variable *result = NULL;

    if (self->type == VAR_STR && sought->type == VAR_STR) {                     // search for substring
        assert_message(!start || start->type == VAR_INT, "non-integer index");
        int32_t beginning = start ? start->integer : 0;
        int32_t index = byte_array_find(self->str, sought->str, beginning);
        if (has)
            result = variable_new_bool(context, index != -1);
        else
            result = variable_new_int(context, index);

    } else if (self->type == VAR_LST) {
        for (int i=0; i<self->list->length; i++) {
            struct variable *v = (struct variable*)array_get(self->list, i);
            if ((sought->type == VAR_INT && v->type == VAR_INT && v->integer == sought->integer) ||
                (sought->type == VAR_STR && v->type == VAR_STR && byte_array_equals(sought->str, v->str)))
                result = has ? variable_new_bool(context, true) : v;
        }
    }
    if (!result && self->map && sought->type == VAR_STR)
        result = (struct variable*)map_get(self->map, sought->str);
    return result ? result : variable_new_nil(context);
}

struct variable *cfnc_find(struct context *context) {
    return cfnc_find2(context, false);
}

struct variable *cfnc_has(struct context *context) {
    return cfnc_find2(context, true);
}

struct variable *cfnc_insert(struct context *context)
{
    struct variable *args = (struct variable*)stack_pop(context->operand_stack);
    struct variable *self = (struct variable*)array_get(args->list, 0);
    struct variable *insertion = (struct variable*)array_get(args->list, 1);
    struct variable *start = args->list->length > 2 ? (struct variable*)array_get(args->list, 2) : NULL;
    null_check(self);
    null_check(insertion);
    assert_message(!start || start->type == VAR_INT, "non-integer index");

    int32_t position = start ? start->integer : 0;
    struct variable *first = variable_part(context, variable_copy(context, self), 0, position);
    struct variable *second = variable_part(context, variable_copy(context, self), position, -1);

    switch (self->type) {
        case VAR_LST: {
            struct array *list = array_new_size(1);
            array_set(list, 0, insertion);
            insertion = variable_new_list(context, list);
        } break;
        case VAR_STR:
            assert_message(insertion->type == VAR_STR, "insertion doesn't match destination");
            break;
        default:
            exit_message("bad insertion destination");
            break;
    }

    struct variable *joined = variable_concatenate(context, 3, first, insertion, second);
    if (self->type == VAR_LST)
        self->list = joined->list;
    else
        self->str = joined->str;
    return joined;
}

/*
struct variable *cfnc_add(struct context *context)
{
    struct variable *args = (struct variable*)stack_pop(context->operand_stack);
    struct variable *self = (struct variable*)array_get(args->list, 0);
    struct variable *insertion = (struct variable*)array_get(args->list, 1);
    assert_message(self->type == VAR_LST || (self->type == VAR_STR && insertion->type == VAR_STR), "insertion doesn't match destination");

    struct variable *inserted;
    if (self->type == inserted->type)
        inserted = variable_concatenate(context, 3, self, insertion);
    else {
//        inserted = variable_copy(context, self);
        array_add(self->list, insertion);
    }
    return NULL;
}
*/

//    a                b        c
// <sought> <replacement> [<start>]
// <start> <length> <replacement>
struct variable *cfnc_replace(struct context *context)
{
    struct variable *args = (struct variable*)stack_pop(context->operand_stack);
    struct variable *self = (struct variable*)array_get(args->list, 0);
    struct variable *a = (struct variable*)array_get(args->list, 1);
    struct variable *b = (struct variable*)array_get(args->list, 2);
    struct variable *c = args->list->length > 3 ? (struct variable*)array_get(args->list, 3) : NULL;

    null_check(self);
    null_check(b);
    assert_message(self->type == VAR_STR, "searching in a non-string");

    int32_t where = 0;
    struct byte_array *replaced = self->str;

    if (a->type == VAR_STR) { // find a, replace with b

        assert_message(b->type == VAR_STR, "non-string replacement");

        if (c) { // replace first match after index b

            assert_message(c->type == VAR_INT, "non-integer index");
            uint32_t found = byte_array_find(self->str, a->str, c->integer);
            replaced = byte_array_replace(self->str, b->str, found, b->str->length);

        } else for(;;) { // replace all

            if ((where = byte_array_find(self->str, a->str, where)) < 0)
                break;
            replaced = byte_array_replace(replaced, b->str, where++, a->str->length);
        }

    } else if (a->type == VAR_INT ) { // replace at index a, length b, insert c

        assert_message(a || a->type == VAR_INT, "non-integer count");
        assert_message(b || b->type == VAR_INT, "non-integer start");
        replaced = byte_array_replace(self->str, c->str, a->integer, b->integer);

    } else exit_message("replacement is not a string");

    null_check(replaced);
    return variable_new_str(context, replaced);
}

struct variable *builtin_method(struct context *context,
                                struct variable *indexable,
                                const struct variable *index)
{
    enum VarType it = indexable->type;
    const char *idxstr = byte_array_to_string(index->str);

    if (!strcmp(idxstr, FNC_LENGTH)) {
        int n;
        switch (indexable->type) {
            case VAR_LST: n = indexable->list->length;  break;
            case VAR_STR: n = indexable->str->length;   break;
            default:
                exit_message("no length for non-indexable");
                return NULL;
        }
        return variable_new_int(context, n);
    }
    if (!strcmp(idxstr, FNC_TYPE)) {
        const char *typestr = var_type_str(it);
        return variable_new_str(context, byte_array_from_string(typestr));
    }

    if (!strcmp(idxstr, FNC_STRING))
        return variable_new_str(context, variable_value(context, indexable));

    if (!strcmp(idxstr, FNC_LIST))
        return variable_new_list(context, indexable->list);

    if (!strcmp(idxstr, FNC_KEYS)) {
        assert_message(it == VAR_LST, "keys are only for list");

        struct variable *v = variable_new_list(context, array_new());
        if (indexable->map) {
            const struct array *a = map_keys(indexable->map);
            for (int i=0; i<a->length; i++) {
                struct variable *u = variable_new_str(context, (struct byte_array*)array_get(a, i));
                array_add(v->list, u);
            }
        }
        return v;
    }

    if (!strcmp(idxstr, FNC_VALUES)) {
        assert_message(it == VAR_LST, "values are only for list");
        if (!indexable->map)
            return variable_new_list(context, array_new());
        else
            return variable_new_list(context, (struct array*)map_values(indexable->map));
    }

    if (!strcmp(idxstr, FNC_SERIALIZE)) {
        struct byte_array *bits = variable_serialize(context, 0, indexable);
        return variable_new_str(context, bits);
    }

    if (!strcmp(idxstr, FNC_DESERIALIZE)) {
        struct byte_array *bits = indexable->str;
        byte_array_reset(bits);
        struct variable *d = variable_deserialize(context, bits);
        return d;
    }

    // todo: don't create a new variable every time
    if (!strcmp(idxstr, FNC_SORT)) {
        assert_message(indexable->type == VAR_LST, "sorting non-list");
        return variable_new_c(context, &cfnc_sort);
    }

    if (!strcmp(idxstr, FNC_CHAR))
        return variable_new_c(context, &cfnc_char);

    if (!strcmp(idxstr, FNC_HAS))
        return variable_new_c(context, &cfnc_has);

    if (!strcmp(idxstr, FNC_FIND))
        return variable_new_c(context, &cfnc_find);

    if (!strcmp(idxstr, FNC_PART))
        return variable_new_c(context, &cfnc_part);

    if (!strcmp(idxstr, FNC_REMOVE))
        return variable_new_c(context, &cfnc_remove);

    if (!strcmp(idxstr, FNC_INSERT))
        return variable_new_c(context, &cfnc_insert);

//    if (!strcmp(idxstr, FNC_ADD))
//        return variable_new_c(context, &cfnc_add);

    if (!strcmp(idxstr, FNC_REPLACE))
        return variable_new_c(context, &cfnc_replace);

    return NULL;
}
