#include "serial.h"
#include "sys.h"
#include "struct.h"
#include "vm.h"
#include "util.h"
#include <stdio.h>
#include <string.h>

extern struct stack *rhs;
extern struct stack *operand_stack;
extern void src_size(int32_t size);
extern void call(struct byte_array *program);
extern struct variable *vm_exception;

// system functions

void print()
{
    stack_pop(rhs); // self
    struct variable *v;
    while ((v = (struct variable*)stack_pop(rhs)))
        printf("%s\n", variable_value(v));
}

void save()
{
    stack_pop(rhs); // self
    struct variable *v = (struct variable*)stack_pop(rhs);
    struct variable *path = (struct variable*)stack_pop(rhs);
    variable_save(v, path);
}

void load()
{
    stack_pop(rhs); // self
    struct variable *path = (struct variable*)stack_pop(rhs);
    struct variable *v = variable_load(path);

    if (v)
        stack_push(operand_stack, v);
}

void rm()
{
    stack_pop(rhs); // self
    struct variable *path = (struct variable*)(struct variable*)stack_pop(rhs);
    remove(byte_array_to_string(path->str));
}

/*
void throw()
{
    stack_pop(rhs); // self
    struct variable *message = stack_pop(rhs);
    vm_assert(message->type == VAR_STR, "non-string error message");
    vm_exception = variable_new_err(byte_array_to_string(message->str));
}
*/

struct string_func builtin_funcs[] = {
    {"print", (bridge*)&print},
    {"save", (bridge*)&save},
    {"load", (bridge*)(bridge*)&load},
    {"remove", (bridge*)&rm},
};

struct variable *func_map()
{
    struct map *map = map_new();
    for (int i=0; i<ARRAY_LEN(builtin_funcs); i++) {
        struct byte_array *name = byte_array_from_string(builtin_funcs[i].name);
        struct variable *value = variable_new_c(builtin_funcs[i].func);
        map_insert(map, name, value);
    }
    return variable_new_map(map);
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
#define FNC_REMOVE		"remove"
#define FNC_INSERT		"insert"

struct variable *comparator = NULL;


struct byte_array *variable_serialize(struct byte_array *bits,
                                      const struct variable *in)
{
    //DEBUGPRINT("\tserialize:%s\n", variable_value(in));
    if (!bits)
        bits = byte_array_new();
    serial_encode_int(bits, 0, in->type);
    switch (in->type) {
        case VAR_INT:    serial_encode_int(bits, 0, in->integer);    break;
        case VAR_FLT:    serial_encode_float(bits, 0, in->floater);    break;
        case VAR_STR:
        case VAR_FNC:    serial_encode_string(bits, 0, in->str);        break;
        case VAR_LST: {
            serial_encode_int(bits, 0, in->list->length);
            for (int i=0; i<in->list->length; i++)
                variable_serialize(bits, (const struct variable*)array_get(in->list, i));
            if (in->map) {
                const struct array *keys = map_keys(in->map);
                const struct array *values = map_values(in->map);
                serial_encode_int(bits, 0, keys->length);
                for (int i=0; i<keys->length; i++) {
                    serial_encode_string(bits, 0, (const struct byte_array*)array_get(keys, i));
                    variable_serialize(bits, (const struct variable*)array_get(values, i));
                }
            } else
                serial_encode_int(bits, 0, 0);
        } break;
        case VAR_MAP:                                                break;
        default:        vm_exit_message("bad var type");                break;
    }

    //DEBUGPRINT("in: %s\n", variable_value(in));
    //byte_array_print("serialized: ", bits);
    return bits;
}

struct variable *variable_deserialize(struct byte_array *bits)
{
    enum VarType vt = (enum VarType)serial_decode_int(bits);
    switch (vt) {
        case VAR_NIL:    return variable_new_nil();
        case VAR_INT:    return variable_new_int(serial_decode_int(bits));
        case VAR_FLT:    return variable_new_float(serial_decode_float(bits));
        case VAR_FNC:    return variable_new_fnc(serial_decode_string(bits));
        case VAR_STR:    return variable_new_str(serial_decode_string(bits));
        case VAR_LST: {
            uint32_t size = serial_decode_int(bits);
            struct array *list = array_new_size(size);
            while (size--)
                array_add(list, variable_deserialize(bits));
            struct variable *out = variable_new_list(list);

            uint32_t map_length = serial_decode_int(bits);
            if (map_length) {
                out->map = map_new();
                for (int i=0; i<map_length; i++) {
                    struct byte_array *key = serial_decode_string(bits);
                    struct variable *value = variable_deserialize(bits);
                    map_insert(out->map, key, value);
                }
            }
            return out;
        }
        default:
            vm_exit_message("bad var type");
            return NULL;
    }
}

int variable_save(const struct variable *v,
                  const struct variable *path)
{
    vm_null_check(v);
    vm_null_check(path);

    struct byte_array *bytes = byte_array_new();
    variable_serialize(bytes, v);
    return write_file(path->str, bytes);
}

struct variable *variable_load(const struct variable *path)
{
    vm_null_check(path);

    struct byte_array *file_bytes = read_file(path->str);
    if (!file_bytes)
        return NULL;
    struct variable *v = variable_deserialize(file_bytes);
    return v;
}

int (compar)(const void *a, const void *b)
{
    struct variable *av = *(struct variable**)a;
    struct variable *bv = *(struct variable**)b;

    if (comparator) {

        assert_message(comparator->type == VAR_FNC, "non-function comparator");
        stack_push(rhs, av);
        stack_push(rhs, bv);
        //        src_size(2);
        byte_array_reset(comparator->str);
        stack_push(operand_stack, comparator);
        vm_call();

        struct variable *result = (struct variable*)stack_pop(rhs);
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

        vm_exit_message("incompatible types for comparison");
        return 0;
    }
}

int testarr[] = { 6, 5, 3, 1, 8, 7, 2, 4 };
//int testarr[] = { 2,1 };
int len = sizeof(testarr)/sizeof(testarr[0]);

void heapset(size_t width, void *base0, uint32_t index0, void *base1, uint32_t index1) {
    uint8_t *p0 = (uint8_t*)base0 + index0 * width;
    uint8_t *p1 = (uint8_t*)base1 + index1 * width;
    while (width--)
        *(p0 + width) = *(p1 + width);
}

int heapcmp(int (*compar)(const void *, const void *), size_t width, void *base0, uint32_t index0, void *base1, uint32_t index1) {
    uint8_t *p0 = (uint8_t*)base0 + index0 * width;
    uint8_t *p1 = (uint8_t*)base1 + index1 * width;
    return compar(p0, p1);
}

int heapsortfg(void *base, size_t nel, size_t width, int (*compar)(const void *, const void *))
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
                heapcmp(compar, width, base, child+1, base, child) > 0) {
                child++; // right child exists and is bigger
            }
            // is the largest child larger than the entry?
            if (heapcmp(compar, width, base, child, t, 0) > 0) {
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

void cfnc_sort(struct stack *operands)
{
    struct variable *self = (struct variable*)stack_pop(rhs);
    assert_message(self->type == VAR_LST, "sorting a non-list");
    comparator = (struct variable*)stack_pop(rhs);

    int num_items = self->list->length;

    int success = heapsortfg(self->list->data, num_items, sizeof(struct variable*), &compar);
    assert_message(!success, "error sorting");
}

struct variable *variable_part(struct variable *self, uint32_t start, int32_t length)
{
	null_check(self);
	switch (self->type) {
		case VAR_STR: {
			struct byte_array *str = byte_array_part(self->str, start, length);
			return variable_new_str(str);
		} break;
		case VAR_LST: {
			struct array *list = array_part(self->list, start, length);
			return variable_new_list(list);
		} break;
		default:
			return exit_message("bad part type");
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

struct variable *variable_concatenate(int n, const struct variable* v, ...)
{
    struct variable* result = variable_copy(v);

    va_list argp;
    for(va_start(argp, v); --n;) {
        struct variable* parameter = va_arg(argp, struct variable* );
        if (!parameter)
            continue;
        else if (!result)
            result = variable_copy(parameter);
        else switch (result->type) {
			case VAR_STR: byte_array_append(result->str, parameter->str); break;
			case VAR_LST: array_append(result->list, parameter->list);    break;
			default: return exit_message("bad concat type");
		}
    }

	va_end(argp);
    return result;
}
/*
struct variable *xvariable_concatenate(struct variable *self, uint32_t start, int32_t length)
{
	null_check(self);
	switch (self->type) {
		case VAR_STR: {
			struct byte_array *str = byte_array_part(self->str, start, length);
			return variable_new_str(str);
		} break;
		case VAR_LST: {
			struct array *list = array_part(self->list, start, length);
			return variable_new_list(list);
		} break;
		default:
			return exit_message("bad part type");
	}
}
*/
void cfnc_chop(struct stack *operands, bool part)
{
    struct variable *self = (struct variable*)stack_pop(rhs);
    struct variable *start = (struct variable*)stack_pop(rhs);
    struct variable *length = (struct variable*)stack_pop(rhs);
    assert_message(start->type == VAR_INT, "non-integer index");
    assert_message(length->type == VAR_INT, "non-integer length");
    int32_t beginning = start->integer;
    int32_t foraslongas = length->integer;

	struct variable *result = variable_copy(self);
	if (part)
		result = variable_part(result, beginning, foraslongas);
	else
		variable_remove(result, beginning, foraslongas);
	stack_push(operand_stack, result);
}

static inline void cfnc_part(struct stack *operands) {
	cfnc_chop(operands, true);
}

static inline void cfnc_remove(struct stack *operands) {
	cfnc_chop(operands, false);
}

void cfnc_find(struct stack *operands)
{
    struct variable *self = (struct variable*)stack_pop(rhs);
    struct variable *sought = (struct variable*)stack_pop(rhs);
    struct variable *start = (struct variable*)stack_pop(rhs);

    null_check(self);
    null_check(sought);
    assert_message(self->type == VAR_STR, "searching in a non-list");
    assert_message(sought->type == VAR_STR, "searching for a non-list");
    if (start) assert_message(start->type == VAR_INT, "non-integer index");

    int32_t beginning = start ? start->integer : 0;
    int32_t index = byte_array_find(self->str, sought->str, beginning);
    struct variable *result = variable_new_int(index);
    stack_push(operand_stack, result);
}

void cfnc_insert(struct stack *operands) // todo: support lst
{
    struct variable *self = (struct variable*)stack_pop(rhs);
    struct variable *start = (struct variable*)stack_pop(rhs);
    struct variable *insertion = (struct variable*)stack_pop(rhs);
    assert_message(start->type == VAR_INT, "non-integer index");
    assert_message(insertion && insertion->type == self->type, "insertion doesn't match destination");

    int32_t position = start->integer;
    struct variable *first = variable_part(variable_copy(self), 0, position);
    struct variable *second = variable_part(variable_copy(self), position, -1);
	struct variable *joined = variable_concatenate(3, first, self, second);
    stack_push(operand_stack, joined);
}

//	a				b		c
// <sought> <replacement> [<start>]
// <start> <length> <replacement>
void replace(struct stack *operands)
{
	struct variable *self = stack_pop(rhs);
	struct variable *a = stack_pop(rhs);
	struct variable *b = stack_pop(rhs);
	struct variable *c = stack_pop(rhs);
	
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
	struct variable *result = variable_new_str(replaced);
	stack_push(operand_stack, result);
}


struct variable *builtin_method(struct variable *indexable,
                                const struct variable *index)
{
    enum VarType it = indexable->type;
    const char *idxstr = byte_array_to_string(index->str);

    if (!strcmp(idxstr, FNC_LENGTH))
        return variable_new_int(indexable->list->length);
    if (!strcmp(idxstr, FNC_TYPE)) {
        const char *typestr = var_type_str(it);
        return variable_new_str(byte_array_from_string(typestr));
    }

    if (!strcmp(idxstr, FNC_STRING))
        return variable_new_str(byte_array_from_string(variable_value(indexable)));

    if (!strcmp(idxstr, FNC_LIST))
        return variable_new_list(indexable->list);

    if (!strcmp(idxstr, FNC_KEYS)) {
        assert_message(it == VAR_LST || it == VAR_MAP, "keys are only for map or list");

        struct variable *v = variable_new_list(array_new());
        if (indexable->map) {
            const struct array *a = map_keys(indexable->map);
            for (int i=0; i<a->length; i++) {
                struct variable *u = variable_new_str((struct byte_array*)array_get(a, i));
                array_add(v->list, u);
            }
        }
        return v;
    }

    if (!strcmp(idxstr, FNC_VALUES)) {
        assert_message(it == VAR_LST || it == VAR_MAP, "values are only for map or list");
        if (!indexable->map)
            return variable_new_list(array_new());
        else
            return variable_new_list((struct array*)map_values(indexable->map));
    }

    if (!strcmp(idxstr, FNC_SERIALIZE)) {
        struct byte_array *bits = variable_serialize(0, indexable);
        return variable_new_str(bits);
    }

    if (!strcmp(idxstr, FNC_DESERIALIZE)) {
        struct byte_array *bits = indexable->str;
        byte_array_reset(bits);
        struct variable *d = variable_deserialize(bits);
        return d;
    }

    if (!strcmp(idxstr, FNC_SORT)) {
        assert_message(indexable->type == VAR_LST, "sorting non-list");
        return variable_new_c(&cfnc_sort);
    }

    if (!strcmp(idxstr, FNC_FIND)) {
        assert_message(indexable->type == VAR_STR, "searching in non-string");
        return variable_new_c(&cfnc_find);
    }

    if (!strcmp(idxstr, FNC_PART))
        return variable_new_c(&cfnc_part);

    if (!strcmp(idxstr, FNC_REMOVE))
        return variable_new_c(&cfnc_remove);

    if (!strcmp(idxstr, FNC_INSERT))
        return variable_new_c(&cfnc_insert);

	if (!strcmp(idxstr, FNC_REPLACE))
		return variable_new_c(&replace);

    return NULL;
}
