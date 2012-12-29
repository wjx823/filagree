#ifndef VARIABLE_H
#define VARIABLE_H

//#include "vm.h"

enum VarType {
    VAR_NIL,
    VAR_INT,
    VAR_FLT,
    VAR_BOOL,
    VAR_STR,
    VAR_FNC,
    VAR_LST,
    VAR_MAP,
    VAR_VST,
    VAR_SRC,
    VAR_ERR,
    VAR_C,
};    

typedef struct context *context_p; // forward declaration
typedef struct variable *(callback2func)(context_p context);
typedef struct variable *(find_c_var)(context_p context, const struct byte_array *name);

struct variable {
    enum VarType type;
    uint32_t marked;
    union {
        struct byte_array* str;
        struct array *list;
        int32_t integer;
        float floater;
        bool boolean;
        struct variable*(*cfnc)(context_p);
    };
    struct map *map;
};

struct variable* variable_new(struct context *context, enum VarType type);
void variable_del(struct context *context, struct variable *v);
struct byte_array* variable_value(struct context *context, struct variable* v);
const char* variable_value_str(struct context *context, struct variable* v);
struct byte_array *variable_serialize(struct context *context, struct byte_array *bits,
                                      const struct variable *in);
struct variable *variable_deserialize(struct context *context, struct byte_array *str);
extern int variable_save(struct context *context, struct variable* v, const struct variable* path);
extern struct variable *variable_load(struct context *context, const struct variable* path);

struct variable* variable_new_bool(struct context *context, bool b);
struct variable *variable_new_err(struct context *context, const char* message);
struct variable *variable_new_c(struct context *context, callback2func *cfnc);
struct variable *variable_new_int(struct context *context, int32_t i);
struct variable *variable_new_nil(struct context *context);
struct variable *variable_new_map(struct context *context, struct map *map);
struct variable *variable_new_float(struct context *context, float f);
struct variable *variable_new_str(struct context *context, struct byte_array *str);
struct variable *variable_new_fnc(struct context *context, struct byte_array *body, struct map *closures);
struct variable *variable_new_list(struct context *context, struct array *list);
struct variable *variable_new_src(struct context *context, uint32_t size);

struct variable *variable_copy(struct context *context, const struct variable *v);
struct variable *variable_pop(struct context *context);
//struct variable *variable_get(struct context *context, const struct variable *v, uint32_t i);
uint32_t variable_length(struct context *context, const struct variable *v);
void variable_push(struct context *context, struct variable *v);
struct variable *variable_concatenate(struct context *context, int n, const struct variable* v, ...);
void variable_remove(struct variable *self, uint32_t start, int32_t length);
struct variable *variable_part(struct context *context, struct variable *self, uint32_t start, int32_t length);

int variable_map_insert(struct variable* v, const struct byte_array *key, struct variable *data);
struct variable *variable_map_get(struct context *context, struct variable* v, const struct byte_array *key);
//int variable_func_env(struct context *context, struct variable* v, const struct byte_array *key, struct variable *data);

const char *var_type_str(enum VarType vt);

#endif // VARIABLE_H
