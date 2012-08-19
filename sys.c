#include <stdio.h>
#include <string.h>
#include "hal.h"
#include "interpret.h"
#include "serial.h"
#include "struct.h"
#include "sys.h"
#include "variable.h"
#include "vm.h"
#include "ui.h"
#include "util.h"

// system functions

void sys_callback2c(struct Context *context)
{
	context->callback2c(context);
}

void sys_print(struct Context *context)
{
    stack_pop(context->operand_stack); // self
    struct variable *v;
    while ((v = (struct variable*)stack_pop(context->operand_stack)))
        printf("%s\n", variable_value_str(context, v));
}

void sys_save(struct Context *context)
{
    struct variable *value = (struct variable*)stack_pop(context->operand_stack);
    struct variable *v = (struct variable*)array_get(value->list, 1); // (struct variable*)stack_pop(context->operand_stack);
    struct variable *path = (struct variable*)array_get(value->list, 2);  // (struct variable*)stack_pop(context->operand_stack);
    variable_save(context, v, path);
}

void sys_load(struct Context *context)
{
    struct variable *value = (struct variable*)stack_pop(context->operand_stack);
    struct variable *path = (struct variable*)array_get(value->list, 1);  // (struct variable*)stack_pop(context->operand_stack);
    struct variable *v = variable_load(context, path);
    if (v)
        stack_push(context->operand_stack, v);
}

void sys_read(struct Context *context)
{
    stack_pop(context->operand_stack); // self
    struct variable *path = (struct variable*)stack_pop(context->operand_stack);
    struct byte_array *bytes = read_file(path->str);
    if (bytes != NULL) {
        struct variable *v = variable_new_str(context, bytes);
        stack_push(context->operand_stack, v);
    } else {
        struct byte_array *str = byte_array_from_string("could not load file");
        context->vm_exception = variable_new_str(context, str);
    }
}

void sys_run(struct Context *context)
{
    stack_pop(context->operand_stack); // self
    struct variable *script = (struct variable*)stack_pop(context->operand_stack);
    char *str = byte_array_to_string(script->str);
    interpret_string(str, NULL);
}

void sys_rm(struct Context *context)
{
    struct variable *value = (struct variable*)stack_pop(context->operand_stack);
    struct variable *path = (struct variable*)array_get(value->list, 1);
    remove(byte_array_to_string(path->str));
}

void sys_window(struct Context *context)
{
    stack_pop(context->operand_stack); // self
    hal_window();
}

void sys_loop(struct Context *context)
{
    stack_pop(context->operand_stack); // self
    hal_loop();
}

void sys_args(struct Context *context)
{
    stack_pop(context->operand_stack); // self
    struct program_state *above = (struct program_state*)stack_peek(context->program_stack, 1);
    struct variable *v = variable_new_list(context, above->args);
    variable_push(context, v);
}

void sys_button(struct Context *context)
{
    stack_pop(context->operand_stack); // self
    int32_t x = ((struct variable*)stack_pop(context->operand_stack))->integer;
    int32_t y = ((struct variable*)stack_pop(context->operand_stack))->integer;
    int32_t w = ((struct variable*)stack_pop(context->operand_stack))->integer;
    int32_t h = ((struct variable*)stack_pop(context->operand_stack))->integer;
    const char *str = byte_array_to_string(((struct variable*)stack_pop(context->operand_stack))->str);
    hal_button(x, y, w, h, str, NULL, NULL);
}

void sys_input(struct Context *context)
{
    stack_pop(context->operand_stack); // self
    int32_t x = ((struct variable*)stack_pop(context->operand_stack))->integer;
    int32_t y = ((struct variable*)stack_pop(context->operand_stack))->integer;
    int32_t w = ((struct variable*)stack_pop(context->operand_stack))->integer;
    int32_t h = ((struct variable*)stack_pop(context->operand_stack))->integer;
    const char *str = byte_array_to_string(((struct variable*)stack_pop(context->operand_stack))->str);
    hal_input(x, y, w, h, str, false);
}

struct string_func builtin_funcs[] = {
	{"yield",   (bridge*)&sys_callback2c},
	{"args",    (bridge*)&sys_args},
    {"print",   (bridge*)&sys_print},
    {"read",    (bridge*)&sys_read},
    {"save",    (bridge*)&sys_save},
    {"load",    (bridge*)&sys_load},
    {"run",     (bridge*)&sys_run},
    {"remove",  (bridge*)&sys_rm},
    {"window",  (bridge*)&sys_window},
    {"loop",    (bridge*)&sys_loop},
    {"button",  (bridge*)&sys_button},
    {"input",   (bridge*)&sys_input},
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
#define FNC_ADD         "add"

int (compar)(struct Context *context, const void *a, const void *b, struct variable *comparator)
{
    struct variable *av = *(struct variable**)a;
    struct variable *bv = *(struct variable**)b;

    if (comparator) {

        assert_message(comparator->type == VAR_FNC, "non-function comparator");
        stack_push(context->operand_stack, av);
        stack_push(context->operand_stack, bv);
        byte_array_reset(comparator->str);
        stack_push(context->operand_stack, comparator);
        vm_call(context);

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
            size_t width, void *base0, uint32_t index0, void *base1, uint32_t index1,
            struct variable *comparator)
{
    uint8_t *p0 = (uint8_t*)base0 + index0 * width;
    uint8_t *p1 = (uint8_t*)base1 + index1 * width;
    return compar(context, p0, p1, comparator);
}

int heapsortfg(struct Context *context, void *base, size_t nel, size_t width, struct variable *comparator)
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

void cfnc_sort(struct Context *context)
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
}

void cfnc_chop(struct Context *context, bool part)
{
    struct variable *args = (struct variable*)stack_pop(context->operand_stack);
    struct variable *self = (struct variable*)array_get(args->list, 0);
    struct variable *start = (struct variable*)array_get(args->list, 1);
    struct variable *length = (struct variable*)array_get(args->list, 2);

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
    struct variable *args = (struct variable*)stack_pop(context->operand_stack);
    struct variable *self = (struct variable*)array_get(args->list, 0);
    struct variable *sought = (struct variable*)array_get(args->list, 1);
    struct variable *start = args->list->length > 2 ? (struct variable*)array_get(args->list, 2) : NULL;

    null_check(self);
    null_check(sought);
    assert_message(self->type == VAR_STR, "searching in a non-list");
    assert_message(sought->type == VAR_STR, "searching for a non-list");
    if (start)
        assert_message(start->type == VAR_INT, "non-integer index");

    int32_t beginning = start ? start->integer : 0;
    int32_t index = byte_array_find(self->str, sought->str, beginning);
    struct variable *result = variable_new_int(context, index);
    stack_push(context->operand_stack, result);
}

void cfnc_insert(struct Context *context)
{
    struct variable *self = (struct variable*)stack_pop(context->operand_stack);
    struct variable *start = (struct variable*)stack_pop(context->operand_stack);
    struct variable *insertion = (struct variable*)stack_pop(context->operand_stack);
    assert_message(start->type == VAR_INT, "non-integer index");
    assert_message(self->type == VAR_LST || (self->type == VAR_STR && insertion->type == VAR_STR), "insertion doesn't match destination");
    
    int32_t position = start->integer;
    struct variable *first = variable_part(context, variable_copy(context, self), 0, position);
    struct variable *second = variable_part(context, variable_copy(context, self), position, -1);
    struct variable *joined = variable_concatenate(context, 3, first, self, second);
    stack_push(context->operand_stack, joined);
}

void cfnc_add(struct Context *context)
{
    struct variable *self = (struct variable*)stack_pop(context->operand_stack);
    struct variable *insertion = (struct variable*)stack_pop(context->operand_stack);
    assert_message(self->type == VAR_LST || (self->type == VAR_STR && insertion->type == VAR_STR), "insertion doesn't match destination");

    struct variable *inserted;
    if (self->type == inserted->type)
        inserted = variable_concatenate(context, 3, self, insertion);
    else {
        inserted = variable_copy(context, self);
        array_add(inserted->list, insertion);
    }
    stack_push(context->operand_stack, inserted);
}

//    a                b        c
// <sought> <replacement> [<start>]
// <start> <length> <replacement>
void replace(struct Context *context)
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
    
    if (!strcmp(idxstr, FNC_ADD))
        return variable_new_c(context, &cfnc_add);
    
    if (!strcmp(idxstr, FNC_REPLACE))
        return variable_new_c(context, &replace);

    return NULL;
}
