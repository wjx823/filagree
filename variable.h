#ifndef VARIABLE_H
#define VARIABLE_H

#include "vm.h"

enum VarType {
    VAR_NIL,
    VAR_INT,
    VAR_FLT,
    VAR_BOOL,
    VAR_STR,
    VAR_FNC,
    VAR_LST,
    VAR_MAP,
    VAR_ERR,
    VAR_C,
};    

struct variable {
    const struct byte_array* name;
    enum VarType type;
    uint8_t marked;
    union {
        struct byte_array* str;
        struct array *list;
        int32_t integer;
        float floater;
        bool boolean;
        void(*cfnc)(context_p, struct stack*); // i.e., bridge
    };
    struct map *map;
};

struct variable* variable_new(struct Context *context, enum VarType type);
void variable_del(struct Context *context, struct variable *v);
const char* variable_value(struct Context *context, const struct variable* v);
struct byte_array *variable_serialize(struct Context *context, struct byte_array *bits,
                                      const struct variable *in);
struct variable *variable_deserialize(struct Context *context, struct byte_array *str);
extern int variable_save(struct Context *context, const struct variable* v, const struct variable* path);
extern struct variable *variable_load(struct Context *context, const struct variable* path);

struct variable* variable_new_bool(struct Context *context, bool b);
struct variable *variable_new_err(struct Context *context, const char* message);
struct variable *variable_new_c(struct Context *context, bridge *cfnc);
struct variable *variable_new_int(struct Context *context, int32_t i);
struct variable *variable_new_nil(struct Context *context);
struct variable *variable_new_map(struct Context *context, struct map *map);
struct variable *variable_new_float(struct Context *context, float f);
struct variable *variable_new_str(struct Context *context, struct byte_array *str);
struct variable *variable_new_fnc(struct Context *context, struct byte_array *fnc);
struct variable *variable_new_list(struct Context *context, struct array *list);
struct variable* variable_copy(struct Context *context, const struct variable* v);
struct variable *variable_pop(struct Context *context);
void variable_push(struct Context *context, struct variable *v);

const char *var_type_str(enum VarType vt);

#endif // VARIABLE_H
