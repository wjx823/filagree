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
    VAR_SRC,
    VAR_ERR,
    VAR_C,
};    

typedef struct Context *context_p; // forward declaration
typedef void(bridge)(context_p context);

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
        void(*cfnc)(context_p); // i.e., bridge
    };
    struct map *map;
};

struct variable* variable_new(struct Context *context, enum VarType type);
void variable_del(struct Context *context, struct variable *v);
struct byte_array* variable_value(struct Context *context, const struct variable* v);
const char* variable_value_str(struct Context *context, const struct variable* v);
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
struct variable *variable_new_src(struct Context *context, uint32_t size);
struct variable *variable_copy(struct Context *context, const struct variable *v);
struct variable *variable_pop(struct Context *context);
//struct variable *variable_get(struct Context *context, const struct variable *v, uint32_t i);
uint32_t variable_length(struct Context *context, const struct variable *v);
void variable_push(struct Context *context, struct variable *v);
struct variable *variable_concatenate(struct Context *context, int n, const struct variable* v, ...);
void variable_remove(struct variable *self, uint32_t start, int32_t length);
struct variable *variable_part(struct Context *context, struct variable *self, uint32_t start, int32_t length);


const char *var_type_str(enum VarType vt);

#endif // VARIABLE_H
