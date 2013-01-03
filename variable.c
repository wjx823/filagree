#include <string.h>
#include "vm.h"
#include "struct.h"
#include "serial.h"
#include "variable.h"
#include "util.h"

#define ERROR_VAR_TYPE  "type error"
#define VAR_MAX         100

const struct number_string var_types[] = {
    {VAR_NIL,   "nil"},
    {VAR_INT,   "integer"},
    {VAR_BOOL,  "boolean"},
    {VAR_FLT,   "float"},
    {VAR_STR,   "string"},
    {VAR_MAP,   "map"},
    {VAR_LST,   "list"},
    {VAR_FNC,   "function"},
    {VAR_VST,   "visited"},
    {VAR_ERR,   "error"},
    {VAR_C,     "c-function"},
};

const char *var_type_str(enum VarType vt)
{
    return NUM_TO_STRING(var_types, vt);
}

struct variable* variable_new(struct context *context, enum VarType type)
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

struct variable* variable_new_nil(struct context *context) {
    return variable_new(context, VAR_NIL);
}

struct variable* variable_new_int(struct context *context, int32_t i)
{
    struct variable *v = variable_new(context, VAR_INT);
    v->integer = i;
    return v;
}

struct variable* variable_new_err(struct context *context, const char* message)
{
    struct variable *v = variable_new(context, VAR_ERR);
    v->str = byte_array_from_string(message);
    return v;
}

struct variable* variable_new_bool(struct context *context, bool b)
{
    struct variable *v = variable_new(context, VAR_BOOL);
    v->boolean = b;
    return v;
}

void variable_del(struct context *context, struct variable *v)
{
    context->num_vars--;
    switch (v->type) {
        case VAR_INT:
        case VAR_FLT:
        case VAR_VST:
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

struct variable *variable_new_src(struct context *context, uint32_t size)
{
    struct variable *v = variable_new(context, VAR_SRC);
    v->list = array_new();

    while (size--) {
        struct variable *o = (struct variable*)stack_pop(context->operand_stack);
        if (o->type == VAR_SRC) {
            array_append(o->list, v->list);
            v = o;
        } else
            array_insert(v->list, 0, o);
    }
//  DEBUGPRINT("src = %s\n", variable_value_str(context, v));
    return v;
}

struct variable* variable_new_float(struct context *context, float f)
{
    //DEBUGPRINT("new float %f\n", f);
    struct variable *v = variable_new(context, VAR_FLT);
    v->floater = f;
    return v;
}

struct variable *variable_new_str(struct context *context, struct byte_array *str) {
    struct variable *v = variable_new(context, VAR_STR);
    v->str = str;
    return v;
}

struct variable *variable_new_fnc(struct context *context, struct byte_array *body, struct map *closures)
{
    struct variable *v = variable_new(context, VAR_FNC);
    v->str = body;
    if (closures) {
        struct variable *vc = variable_new_map(context, closures);
        variable_map_insert(v, byte_array_from_string(RESERVED_ENV), vc);
    }
    return v;
}

struct variable *variable_new_list(struct context *context, struct array *list) {
    struct variable *v = variable_new(context, VAR_LST);
    v->list = list ? list : array_new();
    return v;
}
struct variable *variable_new_map(struct context *context, struct map *map) {
    struct variable *v = variable_new(context, VAR_MAP);
    v->map = map;
    return v;
}

struct variable *variable_new_c(struct context *context, callback2func *cfnc) {
    struct variable *v = variable_new(context, VAR_C);
    v->cfnc = cfnc;
    return v;
}

const char *variable_value_str2(struct context *context, struct variable* v, uint32_t *marker)
{
    null_check(v);
    enum VarType vt = (enum VarType)v->type;
    char* str = (char*)malloc(1000);
    if (v->marked) {
        sprintf(str, "*%d", v->marked);
        return str;
    }
    else if (v->map || vt == VAR_LST) {
        v->marked = *marker;
        (*marker)++;
        sprintf(str, "&%d ", v->marked);
    }

    struct array* list = v->list;

    switch (vt) {
        case VAR_NIL:    sprintf(str, "%snil", str);                               break;
        case VAR_INT:    sprintf(str, "%s%d", str, v->integer);                    break;
        case VAR_BOOL:   sprintf(str, "%s%s", str, v->boolean ? "true" : "false"); break;
        case VAR_FLT:    sprintf(str, "%s%f", str, v->floater);                    break;
        case VAR_STR:    sprintf(str, "%s%s", str, byte_array_to_string(v->str));  break;
        case VAR_FNC:    sprintf(str, "%sf(%dB)", str, v->str->length);            break;
        case VAR_C:      sprintf(str, "%sc-function", str);                        break;
        case VAR_VST:    sprintf(str, "%svisited %d", str, v->integer);            break;
        case VAR_MAP:                                                               break;
        case VAR_SRC:    vt = vt;
        case VAR_LST: {
            strcat(str, "[");
            vm_null_check(context, list);
            for (int i=0; i<list->length; i++) {
                struct variable* element = (struct variable*)array_get(list, i);
                vm_null_check(context, element);
                const char *q = (element->type == VAR_STR || element->type == VAR_FNC) ? "'" : "";
                const char *c = i ? "," : "";
                const char *estr = variable_value_str2(context, element, marker);
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
            strcat(str, "<");
        else if (v->list->length && a->length)
            strcat(str, ",");
        for (int i=0; i<a->length; i++) {
            if (i)
                strcat(str, ",");
            strcat(str, "'");
            strcat(str, byte_array_to_string((struct byte_array*)array_get(a,i)));
            strcat(str, "'");
            strcat(str, ":");
            struct variable *biv = (struct variable*)array_get(b,i);
            const char *bistr = variable_value_str2(context, biv, marker);
            strcat(str, bistr);
        }
        strcat(str, vt==VAR_LST ? "]" : ">");
    }
    else if (vt == VAR_LST || vt == VAR_SRC)
        strcat(str, "]");

    return str;
}

void variable_unmark(struct variable *v)
{
    if (!v->marked)
        return;
    v->marked = false;
    if (v->type == VAR_LST) {
        for (int i=0; i<v->list->length; i++) {
            struct variable* element = (struct variable*)array_get(v->list, i);
            variable_unmark(element);
        }
    }
    if (v->map) {
        const struct array *a = map_keys(v->map);
        const struct array *b = map_values(v->map);
        for (int i=0; i<a->length; i++) {
            struct variable *biv = (struct variable*)array_get(b,i);
            variable_unmark(biv);
        }
    }
}


const char *variable_value_str(struct context *context, struct variable* v)
{
    uint32_t marker = 1;
    variable_unmark(v);
    const char *str = variable_value_str2(context, v, &marker);
    variable_unmark(v);
    return str;
}

struct byte_array *variable_value(struct context *c, struct variable *v) {
    const char *str = variable_value_str(c, v);
    return byte_array_from_string(str);
}

struct variable *variable_pop(struct context *context)
{
    struct variable *v = (struct variable*)stack_pop(context->operand_stack);
    null_check(v);
//    DEBUGPRINT("\nvariable_pop %s\n", variable_value_str(context, v));
//    print_operand_stack(context);
    if (v->type == VAR_SRC) {
//        DEBUGPRINT("\tsrc %d ", v->list->length);
        if (v->list->length)
            v = (struct variable*)array_get(v->list, 0);
        else
            v = variable_new_nil(context);
    }
    return v;
}

void variable_push(struct context *context, struct variable *v)
{
    stack_push(context->operand_stack, v);
}

// builds a map of variables, marking which ones are referenced in other variables
void variable_cycle_check(struct variable *v,
                          struct variable *visited)
{
    assert_message(visited->type == VAR_INT, "wrong visited type");

    int32_t index = (VOID_INT)map_get(visited->map, v);
    if (index) {  // another variable points to this one
        v->marked = true;
        return;
    }
    map_insert(visited->map, v, (void*)(VOID_INT)(visited->integer++));

    if (v->type == VAR_LST)
        for (int i=0; i<v->list->length; i++)
            variable_cycle_check((struct variable*)array_get(v->list, i), visited);

    if (v->map) {
        const struct array *values = map_values(v->map);
        for (int i=0; i<values->length; i++)
            variable_cycle_check((struct variable*)array_get(values, i), visited);
    }

}

struct byte_array *variable_serialize(struct context *context,
									  struct byte_array *bits,
                                      const struct variable *in,
                                      bool withType)
{
	null_check(context);
    //DEBUGPRINT("\tserialize:%s\n", variable_value_str(context, (struct variable*)in));
    if (!bits)
        bits = byte_array_new();
    if (withType)
        serial_encode_int(bits, in->type);
    switch (in->type) {
        case VAR_INT:    serial_encode_int(bits, in->integer);    break;
        case VAR_FLT:    serial_encode_float(bits, in->floater);    break;
        case VAR_STR:
        case VAR_FNC:    serial_encode_string(bits, in->str);        break;
        case VAR_LST: {
            serial_encode_int(bits, in->list->length);
            for (int i=0; i<in->list->length; i++)
                variable_serialize(context, bits, (const struct variable*)array_get(in->list, i), true);
            if (in->map) {
                const struct array *keys = map_keys(in->map);
                const struct array *values = map_values(in->map);
                serial_encode_int(bits, keys->length);
                for (int i=0; i<keys->length; i++) {
                    serial_encode_string(bits, (const struct byte_array*)array_get(keys, i));
                    variable_serialize(context, bits, (const struct variable*)array_get(values, i), true);
                }
            } else
                serial_encode_int(bits, 0);
        } break;
        default:        vm_exit_message(context, "bad var type");                break;
    }
    
    //DEBUGPRINT("in: %s\n", variable_value(in));
    //byte_array_print("serialized: ", bits);
    return bits;
}

struct variable *variable_deserialize(struct context *context, struct byte_array *bits)
{
	null_check(context);
    enum VarType vt = (enum VarType)serial_decode_int(bits);
    switch (vt) {
        case VAR_NIL:    return variable_new_nil(context);
        case VAR_INT:    return variable_new_int(context, serial_decode_int(bits));
        case VAR_FLT:    return variable_new_float(context, serial_decode_float(bits));
        case VAR_FNC:    return variable_new_fnc(context, serial_decode_string(bits), NULL);
        case VAR_STR:    return variable_new_str(context, serial_decode_string(bits));
        case VAR_LST: {
            uint32_t size = serial_decode_int(bits);
            struct array *list = array_new_size(size);
            while (size--)
                array_add(list, variable_deserialize(context, bits));
            struct variable *out = variable_new_list(context, list);
            
            uint32_t map_length = serial_decode_int(bits);
            if (map_length) {
                out->map = map_new(NULL, NULL);
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

/*
struct byte_array *variable_serialize2(struct context *context,
                                       struct byte_array *bits,
                                       struct variable *in,
                                       struct variable *visited)
{
    assert_message(visited->type == VAR_INT, "wrong visited type");
    if (in->marked)
        return bits;

    int type;
    int32_t index = (int32_t)map_get(visited->map, in);
    if (index && !in->marked) {
        type = VAR_VST;
        in->marked = true;
    } else {
        type = in->type;
        map_insert(visited->map, in, (void*)(++visited->integer));
    }

    //DEBUGPRINT("\tserialize:%s\n", variable_value(in));
    if (!bits)
        bits = byte_array_new();
    serial_encode_int(bits, 0, type);
    serial_encode_int(bits, 0, index);
    switch (type) {
        case VAR_STR:
        case VAR_FNC:   serial_encode_string(bits, 0, in->str);     break;
        case VAR_VST:                                               break;
        case VAR_INT:   serial_encode_int(bits, 0, in->integer);    break;
        case VAR_FLT:   serial_encode_float(bits, 0, in->floater);  break;
        case VAR_LST: {
            serial_encode_int(bits, 0, in->list->length);
            for (int i=0; i<in->list->length; i++)
                variable_serialize2(context, bits, (struct variable*)array_get(in->list, i), visited);
        } break;
        default:
            vm_exit_message(context, "bad var type");
            break;
    }

    if (in->map) {
        assert_message(in->map->type==MAP_KEY_BYTE_ARRAY, "can only serialize map of byte-array keys");
        const struct array *keys = map_keys(in->map);
        const struct array *values = map_values(in->map);
        serial_encode_int(bits, 0, keys->length);
        for (int i=0; i<keys->length; i++) {
            serial_encode_string(bits, 0, (const struct byte_array*)array_get(keys, i));
            variable_serialize2(context, bits, (struct variable*)array_get(values, i), visited);
        }
    } else
        serial_encode_int(bits, 0, 0);

    //DEBUGPRINT("in: %s\n", variable_value(in));
    //byte_array_print("serialized: ", bits);
    return bits;
}

struct variable *variable_deserialize2(struct context *context, struct byte_array *bits, struct map *visited)
{
    struct variable *out = NULL;
    enum VarType vt = (enum VarType)serial_decode_int(bits);
    int index = serial_decode_int(bits);
    switch (vt) {
        case VAR_VST:   return (struct variable*)map_get(visited, (void*)index);
        case VAR_NIL:   out = variable_new_nil(context);
        case VAR_INT:   out = variable_new_int(context, serial_decode_int(bits));
        case VAR_FLT:   out = variable_new_float(context, serial_decode_float(bits));
        case VAR_FNC:   out = variable_new_fnc(context, serial_decode_string(bits), NULL);
        case VAR_STR:   out = variable_new_str(context, serial_decode_string(bits));
        case VAR_LST: {
            uint32_t size = serial_decode_int(bits);
            struct array *list = array_new_size(size);
            while (size--)
                array_add(list, variable_deserialize(context, bits));
            out = variable_new_list(context, list);
        }
        default:
            vm_exit_message(context, "bad var type");
            return NULL;
    }

    uint32_t map_length = serial_decode_int(bits);
    if (map_length) {
        out->map = map_new(MAP_KEY_BYTE_ARRAY);
        for (int i=0; i<map_length; i++) {
            struct byte_array *key = serial_decode_string(bits);
            struct variable *value = variable_deserialize(context, bits);
            map_insert(out->map, key, value);
        }
    }
    if (index)
        map_insert(visited, (void*)index, out);
    return out;
}


// todo: rewrite in filagree
struct byte_array *variable_serialize(struct context *context,
                                      struct byte_array *bits,
                                      struct variable *in)
{
    struct variable *visited = variable_new_int(context, 0);
    visited->map = map_new(MAP_KEY_VOID_STAR);
    variable_cycle_check(in, visited);
    return variable_serialize2(context, bits, in, visited);
}

struct variable *variable_deserialize(struct context *context, struct byte_array *bits)
{
    struct map *visited = map_new(MAP_KEY_VOID_STAR);
    return variable_deserialize2(context, bits, visited);
}
*/

int variable_save(struct context *context,
                  struct variable *v,
                  const struct variable *path)
{
    vm_null_check(context, v);
    vm_null_check(context, path);

    struct byte_array *bytes = byte_array_new();
    variable_serialize(context, bytes, v, true);
    return write_file(path->str, bytes);
}

struct variable *variable_load(struct context *context, const struct variable *path)
{
    vm_null_check(context, path);

    struct byte_array *file_bytes = read_file(path->str);
    if (!file_bytes)
        return NULL;
    struct variable *v = variable_deserialize(context, file_bytes);
    return v;
}

/*struct variable *variable_get(struct context *context, const struct variable *v, uint32_t i)
{
    switch (v->type) {
        case VAR_LST: return (struct variable*)array_get(v->list, i);
        case VAR_STR: return variable_new_str(context, byte_array_part(v->str, i, 1));
        default:      return vm_exit_message(context, "non-indexable get");
    }
}*/

uint32_t variable_length(struct context *context, const struct variable *v)
{
    switch (v->type) {
        case VAR_LST: return v->list->length;
        case VAR_STR: return v->str->length;
        case VAR_NIL: return 0;
        default:
            vm_exit_message(context, "non-indexable length");
            return 0;
    }
}

struct variable *variable_part(struct context *context, struct variable *self, uint32_t start, int32_t length)
{
    null_check(self);

    if (length < 0) // count back from end of list/string
        length = self->list->length + length + 1 - start;
    if (length < 0) // end < start
        length = 0;

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

struct variable *variable_concatenate(struct context *context, int n, const struct variable* v, ...)
{
    struct variable* result = variable_copy(context, v);

    va_list argp;
    for(va_start(argp, v); --n;) {
        struct variable* parameter = va_arg(argp, struct variable* );
        if (!parameter)
            continue;
//        else if (!result)
//            result = variable_copy(context, parameter);
        else switch (result->type) {
            case VAR_STR: byte_array_append(result->str, parameter->str); break;
            case VAR_LST: array_append(result->list, parameter->list);    break;
            default: return (struct variable*)exit_message("bad concat type");
        }
    }

    va_end(argp);
    return result;
}

int variable_map_insert(struct variable* v, const struct byte_array *key, struct variable *datum)
{
    if (!v->map)
        v->map = map_new(NULL, NULL);
    return map_insert(v->map, key, datum);
}

struct variable *variable_map_get(struct context *context, struct variable* v, const struct byte_array *key)
{
    if (!v->map)
        return variable_new_nil(context);
    return (struct variable*)map_get(v->map, key);
}

/*
int variable_func_env(struct context *context, struct variable* f, const struct byte_array *key, struct variable *datum)
{
    null_check(f);
    null_check(key);
    null_check(datum);
    assert_message(f->type == VAR_FNC, "non-func for env");
    struct byte_array *renv = byte_array_from_string(RESERVED_ENV);
    struct variable *env = (struct variable*)variable_map_get(context, f, renv);
    if (env->type == VAR_NIL)
        env = variable_new_map(context, NULL);
    int result = variable_map_insert(env, key, datum) || variable_map_insert(f, renv, env);
    return result;
}
*/