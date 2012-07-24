#include "serial.h"
#include "sys.h"
#include "struct.h"
#include "variable.h"
#include "vm.h"
#include "util.h"
#include <stdio.h>
#include <string.h>
#include "ui.h"

// system functions

void sys_callback2c(struct Context *context)
{
    stack_pop(context->rhs); // self
	context->callback2c(context);
}

void sys_print(struct Context *context)
{
    stack_pop(context->rhs); // self
    struct variable *v;
    while ((v = (struct variable*)stack_pop(context->rhs)))
        printf("%s\n", variable_value_str(context, v));
}

void sys_save(struct Context *context)
{
    stack_pop(context->rhs); // self
    struct variable *v = (struct variable*)stack_pop(context->rhs);
    struct variable *path = (struct variable*)stack_pop(context->rhs);
    variable_save(context, v, path);
}

void sys_load(struct Context *context)
{
    stack_pop(context->rhs); // self
    struct variable *path = (struct variable*)stack_pop(context->rhs);
    struct variable *v = variable_load(context, path);
    if (v)
        stack_push(context->operand_stack, v);
}

void sys_rm(struct Context *context)
{
    stack_pop(context->rhs); // self
    struct variable *path = (struct variable*)stack_pop(context->rhs);
    remove(byte_array_to_string(path->str));
}

void sys_args(struct Context *context)
{
    stack_pop(context->rhs); // self
    struct variable *result = variable_new_list(context, context->args);
    stack_push(context->operand_stack, result);
}

struct string_func builtin_funcs[] = {
	{"yield",  (bridge*)&sys_callback2c},
    {"print",  (bridge*)&sys_print},
    {"save",   (bridge*)&sys_save},
    {"load",   (bridge*)&sys_load},
    {"remove", (bridge*)&sys_rm},
    {"args",   (bridge*)&sys_args}
};

struct variable *func_map(struct Context *context)
{
    struct map *map = map_new();
    for (int i=0; i<ARRAY_LEN(builtin_funcs); i++) {
        struct byte_array *name = byte_array_from_string(builtin_funcs[i].name);
        struct variable *value = variable_new_c(context, builtin_funcs[i].func);
        map_insert(map, name, value);
    }
    return variable_new_map(context, map);
}

// built-in member functions

#define FNC_STRING      "string"
#define FNC_LIST        "list"
#define FNC_TYPE        "type"
#define FNC_LENGTH      "length"
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

struct variable *comparator = NULL;

int (compar)(struct Context *context, const void *a, const void *b)
{
    struct variable *av = *(struct variable**)a;
    struct variable *bv = *(struct variable**)b;

    if (comparator) {

        assert_message(comparator->type == VAR_FNC, "non-function comparator");
        stack_push(context->rhs, av);
        stack_push(context->rhs, bv);
        byte_array_reset(comparator->str);
        stack_push(context->operand_stack, comparator);
        vm_call(context);

        struct variable *result = (struct variable*)stack_pop(context->rhs);
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

/*int testarr[] = { 6, 5, 3, 1, 8, 7, 2, 4 };
//int testarr[] = { 2,1 };
int len = sizeof(testarr)/sizeof(testarr[0]);*/

void heapset(size_t width, void *base0, uint32_t index0, void *base1, uint32_t index1) {
    uint8_t *p0 = (uint8_t*)base0 + index0 * width;
    uint8_t *p1 = (uint8_t*)base1 + index1 * width;
    while (width--)
        *(p0 + width) = *(p1 + width);
}

int heapcmp(struct Context *context,
            int (*compar)(struct Context *, const void *, const void *),
            size_t width, void *base0, uint32_t index0, void *base1, uint32_t index1) {
    uint8_t *p0 = (uint8_t*)base0 + index0 * width;
    uint8_t *p1 = (uint8_t*)base1 + index1 * width;
    return compar(context, p0, p1);
}

int heapsortfg(struct Context *context,
               void *base, size_t nel, size_t width,
               int (*compar)(struct Context *, const void *, const void *))
{
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
                heapcmp(context, compar, width, base, child+1, base, child) > 0) {
                child++; // right child exists and is bigger
            }
            // is the largest child larger than the entry?
            if (heapcmp(context, compar, width, base, child, t, 0) > 0) {
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

void cfnc_sort(struct Context *context)
{
    struct variable *self = (struct variable*)stack_pop(context->rhs);
    assert_message(self->type == VAR_LST, "sorting a non-list");
    comparator = (struct variable*)stack_pop(context->rhs);

    int num_items = self->list->length;

    int success = heapsortfg(context, self->list->data, num_items, sizeof(struct variable*), &compar);
    assert_message(!success, "error sorting");
}

struct variable *variable_part(struct Context *context, struct variable *self, uint32_t start, int32_t length)
{
    null_check(self);
    switch (self->type) {
        case VAR_STR: {
            struct byte_array *str = byte_array_part(self->str, start, length);
            return variable_new_str(context, str);
        }
        case VAR_LST: {
            struct array *list = array_part(self->list, start, length);
            return variable_new_list(context, list);
        }
        default:
            return (struct variable*)exit_message("bad part type");
    }
}

void variable_remove(struct variable *self, uint32_t start, int32_t length)
{
    null_check(self);
    switch (self->type) {
        case VAR_STR:
            byte_array_remove(self->str, start, length);
            break;
        case VAR_LST:
            array_remove(self->list, start, length);
            break;
        default:
            exit_message("bad remove type");
    }
}

struct variable *variable_concatenate(struct Context *context, int n, const struct variable* v, ...)
{
    struct variable* result = variable_copy(context, v);

    va_list argp;
    for(va_start(argp, v); --n;) {
        struct variable* parameter = va_arg(argp, struct variable* );
        if (!parameter)
            continue;
        else if (!result)
            result = variable_copy(context, parameter);
        else switch (result->type) {
            case VAR_STR: byte_array_append(result->str, parameter->str); break;
            case VAR_LST: array_append(result->list, parameter->list);    break;
            default: return (struct variable*)exit_message("bad concat type");
        }
    }

    va_end(argp);
    return result;
}

void cfnc_chop(struct Context *context, bool part)
{
    struct variable *self = (struct variable*)stack_pop(context->rhs);
    struct variable *start = (struct variable*)stack_pop(context->rhs);
    struct variable *length = (struct variable*)stack_pop(context->rhs);
    assert_message(start->type == VAR_INT, "non-integer index");
    assert_message(length->type == VAR_INT, "non-integer length");
    int32_t beginning = start->integer;
    int32_t foraslongas = length->integer;

    struct variable *result = variable_copy(context, self);
    if (part)
        result = variable_part(context, result, beginning, foraslongas);
    else
        variable_remove(result, beginning, foraslongas);
    stack_push(context->operand_stack, result);
}

static inline void cfnc_part(struct Context *context) {
    cfnc_chop(context, true);
}

static inline void cfnc_remove(struct Context *context) {
    cfnc_chop(context, false);
}

void cfnc_find(struct Context *context)
{
    struct variable *self = (struct variable*)stack_pop(context->rhs);
    struct variable *sought = (struct variable*)stack_pop(context->rhs);
    struct variable *start = (struct variable*)stack_pop(context->rhs);

    null_check(self);
    null_check(sought);
    assert_message(self->type == VAR_STR, "searching in a non-list");
    assert_message(sought->type == VAR_STR, "searching for a non-list");
    if (start) assert_message(start->type == VAR_INT, "non-integer index");

    int32_t beginning = start ? start->integer : 0;
    int32_t index = byte_array_find(self->str, sought->str, beginning);
    struct variable *result = variable_new_int(context, index);
    stack_push(context->operand_stack, result);
}

void cfnc_insert(struct Context *context)
{
    struct variable *self = (struct variable*)stack_pop(context->rhs);
    struct variable *start = (struct variable*)stack_pop(context->rhs);
    struct variable *insertion = (struct variable*)stack_pop(context->rhs);
    assert_message(start->type == VAR_INT, "non-integer index");
    assert_message(insertion && insertion->type == self->type, "insertion doesn't match destination");

    int32_t position = start->integer;
    struct variable *first = variable_part(context, variable_copy(context, self), 0, position);
    struct variable *second = variable_part(context, variable_copy(context, self), position, -1);
    struct variable *joined = variable_concatenate(context, 3, first, self, second);
    stack_push(context->operand_stack, joined);
}

//    a                b        c
// <sought> <replacement> [<start>]
// <start> <length> <replacement>
void replace(struct Context *context)
{
    struct variable *self = (struct variable*)stack_pop(context->rhs);
    struct variable *a = (struct variable*)stack_pop(context->rhs);
    struct variable *b = (struct variable*)stack_pop(context->rhs);
    struct variable *c = (struct variable*)stack_pop(context->rhs);
    
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
    struct variable *result = variable_new_str(context, replaced);
    stack_push(context->operand_stack, result);
}


struct variable *builtin_method(struct Context *context, 
                                struct variable *indexable,
                                const struct variable *index)
{
    enum VarType it = indexable->type;
    const char *idxstr = byte_array_to_string(index->str);

    if (!strcmp(idxstr, FNC_LENGTH))
        return variable_new_int(context, indexable->list->length);
    if (!strcmp(idxstr, FNC_TYPE)) {
        const char *typestr = var_type_str(it);
        return variable_new_str(context, byte_array_from_string(typestr));
    }

    if (!strcmp(idxstr, FNC_STRING))
        return variable_new_str(context, variable_value(context, indexable));

    if (!strcmp(idxstr, FNC_LIST))
        return variable_new_list(context, indexable->list);

    if (!strcmp(idxstr, FNC_KEYS)) {
        assert_message(it == VAR_LST || it == VAR_MAP, "keys are only for map or list");

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
        assert_message(it == VAR_LST || it == VAR_MAP, "values are only for map or list");
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

    if (!strcmp(idxstr, FNC_SORT)) {
        assert_message(indexable->type == VAR_LST, "sorting non-list");
        return variable_new_c(context, &cfnc_sort);
    }

    if (!strcmp(idxstr, FNC_FIND)) {
        assert_message(indexable->type == VAR_STR, "searching in non-string");
        return variable_new_c(context, &cfnc_find);
    }

    if (!strcmp(idxstr, FNC_PART))
        return variable_new_c(context, &cfnc_part);

    if (!strcmp(idxstr, FNC_REMOVE))
        return variable_new_c(context, &cfnc_remove);

    if (!strcmp(idxstr, FNC_INSERT))
        return variable_new_c(context, &cfnc_insert);

    if (!strcmp(idxstr, FNC_REPLACE))
        return variable_new_c(context, &replace);

    return NULL;
}
