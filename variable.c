#include <string.h>
/*#include <stdio.h>
#include <signal.h>
#include <stdarg.h>
#include <time.h>
#include "util.h"
#include "sys.h"
*/
#include "vm.h"
#include "struct.h"
#include "serial.h"
#include "variable.h"
#include "util.h"

#define    ERROR_VAR_TYPE    "type error"
#define VAR_MAX    100

const struct number_string var_types[] = {
    {VAR_NIL,        "nil"},
    {VAR_INT,        "integer"},
    {VAR_BOOL,        "boolean"},
    {VAR_FLT,        "float"},
    {VAR_STR,        "string"},
    {VAR_LST,        "list"},
    {VAR_FNC,        "function"},
    {VAR_MAP,        "map"},
    {VAR_ERR,        "error"},
    {VAR_C,            "c-function"},
};

const char *var_type_str(enum VarType vt)
{
    return NUM_TO_STRING(var_types, vt);
}

struct variable* variable_new(struct Context *context, enum VarType type)
{
	null_check(context);
   if (context->num_vars++ > VAR_MAX)
        garbage_collect(context);
    struct variable* v = (struct variable*)malloc(sizeof(struct variable));
    v->type = type;
    v->map = NULL;
    v->marked = false;
    return v;
}

struct variable* variable_new_err(struct Context *context, const char* message)
{
	null_check(context);
    struct variable *v = variable_new(context, VAR_ERR);
    v->str = byte_array_from_string(message);
    return v;
}

struct variable* variable_new_nil(struct Context *context)
{
	null_check(context);
    return variable_new(context, VAR_NIL);
}

struct variable* variable_new_int(struct Context *context, int32_t i)
{
	null_check(context);
    struct variable *v = variable_new(context, VAR_INT);
    v->integer = i;
    return v;
}

struct variable* variable_new_bool(struct Context *context, bool b)
{
	null_check(context);
    struct variable *v = variable_new(context, VAR_BOOL);
    v->boolean = b;
    return v;
}

void variable_del(struct Context *context, struct variable *v)
{
	null_check(context);
    context->num_vars--;
    switch (v->type) {
        case VAR_INT:
        case VAR_FLT:
        case VAR_MAP:
            break;
        case VAR_STR:
        case VAR_FNC:
            byte_array_del(v->str);
            break;
        case VAR_LST:
            for (int i=0; i<v->list->length; i++)
                variable_del(context, (struct variable*)array_get(v->list, i));
            break;
        default:
            vm_exit_message(context, "bad var type");
            break;
    }
    if (v->map) {
        struct array *keys = map_keys(v->map);
        struct array *values = map_values(v->map);
        for (int i=0; i<keys->length; i++) {
            byte_array_del((struct byte_array*)array_get(keys, i));
            variable_del(context, (struct variable*)array_get(values, i));
        }
        array_del(keys);
        array_del(values);
        map_del(v->map);
    }
    free(v);
}

struct variable* variable_new_float(struct Context *context, float f)
{
	null_check(context);
    //DEBUGPRINT("new float %f\n", f);
    struct variable *v = variable_new(context, VAR_FLT);
    v->floater = f;
    return v;
}

struct variable *variable_new_str(struct Context *context, struct byte_array *str) {
    struct variable *v = variable_new(context, VAR_STR);
    v->str = str;
    return v;
}

struct variable *variable_new_fnc(struct Context *context, struct byte_array *fnc) {
    struct variable *v = variable_new(context, VAR_FNC);
    v->str = fnc;
    return v;
}

struct variable *variable_new_list(struct Context *context, struct array *list) {
    struct variable *v = variable_new(context, VAR_LST);
    v->list = list ? list : array_new();
    return v;
}

struct variable *variable_new_map(struct Context *context, struct map *map) {
    struct variable *v = variable_new(context, VAR_MAP);
    v->map = map;
    return v;
}

struct variable *variable_new_c(struct Context *context, bridge *cfnc) {
    struct variable *v = variable_new(context, VAR_C);
    v->cfnc = cfnc;
    return v;
}

const char *variable_value_str(struct Context *context, const struct variable* v)
{
	null_check(v);
    char* str = (char*)malloc(100);
    struct array* list = v->list;
	
    enum VarType vt = (enum VarType)v->type;
    switch (vt) {
        case VAR_NIL:    sprintf(str, "nil");                                    break;
        case VAR_INT:    sprintf(str, "%d", v->integer);                         break;
        case VAR_BOOL:   sprintf(str, "%s", v->boolean ? "true" : "false");      break;
        case VAR_FLT:    sprintf(str, "%f", v->floater);                         break;
        case VAR_STR:    sprintf(str, "%s", byte_array_to_string(v->str));       break;
        case VAR_FNC:    sprintf(str, "f(%dB)", v->str->length);                 break;
        case VAR_C:      sprintf(str, "c-function");                             break;
        case VAR_MAP:                                                            break;
        case VAR_LST: {
            strcpy(str, "[");
            vm_null_check(context, list);
            for (int i=0; i<list->length; i++) {
                struct variable* element = (struct variable*)array_get(list, i);
                vm_null_check(context, element);
                const char *q = (element->type == VAR_STR || element->type == VAR_FNC) ? "'" : "";
                const char *c = i ? "," : "";
				const char *estr = variable_value_str(context, element);
                sprintf(str, "%s%s%s%s%s", str, c, q, estr, q);
            }
        } break;
        case VAR_ERR:
            strcpy(str, byte_array_to_string(v->str));
            break;
        default:
            vm_exit_message(context, ERROR_VAR_TYPE);
            break;
    }

    if (v->map) {
        const struct array *a = map_keys(v->map);
        const struct array *b = map_values(v->map);
		
        if (vt != VAR_LST)
            strcat(str, "[");
        else if (v->list->length && a->length)
            strcat(str, ",");
        for (int i=0; i<a->length; i++) {
            if (i)
                strcat(str, ",");
            strcat(str, "'");
            strcat(str, byte_array_to_string((struct byte_array*)array_get(a,i)));
            strcat(str, "'");
            strcat(str, ":");
			const struct variable *biv = (const struct variable*)array_get(b,i);
			const char *bistr = variable_value_str(context, biv);
            strcat(str, bistr);
        }
        strcat(str, "]");
    }
    else if (vt == VAR_LST)
        strcat(str, "]");

	return str;
}

struct byte_array *variable_value(struct Context *c, const struct variable *v) {
	const char *str = variable_value_str(c, v);
    return byte_array_from_string(str);
}



struct variable *variable_pop(struct Context *context)
{
	null_check(context);
    struct variable *v = (struct variable*)stack_pop(context->operand_stack);
    //DEBUGPRINT("\nvariable_pop\n");// %s\n", variable_value(v));
    //    print_operand_stack();
    return v;
}

void variable_push(struct Context *context, struct variable *v)
{
	null_check(context);
    stack_push(context->operand_stack, v);
    //DEBUGPRINT("\nvariable_push\n");
    //print_operand_stack();
}

struct byte_array *variable_serialize(struct Context *context, 
									  struct byte_array *bits,
                                      const struct variable *in)
{
	null_check(context);
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
                variable_serialize(context, bits, (const struct variable*)array_get(in->list, i));
            if (in->map) {
                const struct array *keys = map_keys(in->map);
                const struct array *values = map_values(in->map);
                serial_encode_int(bits, 0, keys->length);
                for (int i=0; i<keys->length; i++) {
                    serial_encode_string(bits, 0, (const struct byte_array*)array_get(keys, i));
                    variable_serialize(context, bits, (const struct variable*)array_get(values, i));
                }
            } else
                serial_encode_int(bits, 0, 0);
        } break;
        case VAR_MAP:                                                break;
        default:        vm_exit_message(context, "bad var type");                break;
    }
	
    //DEBUGPRINT("in: %s\n", variable_value(in));
    //byte_array_print("serialized: ", bits);
    return bits;
}

struct variable *variable_deserialize(struct Context *context, struct byte_array *bits)
{
	null_check(context);
    enum VarType vt = (enum VarType)serial_decode_int(bits);
    switch (vt) {
        case VAR_NIL:    return variable_new_nil(context);
        case VAR_INT:    return variable_new_int(context, serial_decode_int(bits));
        case VAR_FLT:    return variable_new_float(context, serial_decode_float(bits));
        case VAR_FNC:    return variable_new_fnc(context, serial_decode_string(bits));
        case VAR_STR:    return variable_new_str(context, serial_decode_string(bits));
        case VAR_LST: {
            uint32_t size = serial_decode_int(bits);
            struct array *list = array_new_size(size);
            while (size--)
                array_add(list, variable_deserialize(context, bits));
            struct variable *out = variable_new_list(context, list);
			
            uint32_t map_length = serial_decode_int(bits);
            if (map_length) {
                out->map = map_new();
                for (int i=0; i<map_length; i++) {
                    struct byte_array *key = serial_decode_string(bits);
                    struct variable *value = variable_deserialize(context, bits);
                    map_insert(out->map, key, value);
                }
            }
            return out;
        }
        default:
            vm_exit_message(context, "bad var type");
            return NULL;
    }
}

int variable_save(struct Context *context, 
				  const struct variable *v,
                  const struct variable *path)
{
    vm_null_check(context, v);
    vm_null_check(context, path);
	
    struct byte_array *bytes = byte_array_new();
    variable_serialize(context, bytes, v);
    return write_file(path->str, bytes);
}

struct variable *variable_load(struct Context *context, const struct variable *path)
{
	null_check(context);
    vm_null_check(context, path);
	
    struct byte_array *file_bytes = read_file(path->str);
    if (!file_bytes)
        return NULL;
    struct variable *v = variable_deserialize(context, file_bytes);
    return v;
}

