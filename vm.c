#include <stdio.h>
#include <signal.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>
#include "util.h"
#include "serial.h"
#include "vm.h"


void garbage_collect();
struct lifo *program_stack;
struct lifo *operand_stack;
uint32_t num_inst_executed = 0;

#ifdef DEBUG

void display_instruction(struct byte_array *program);
void display_code(struct byte_array *code);
void print_operand_stack();
struct variable* run(struct byte_array *program);
struct variable* variable_new_err(const char* message);
struct variable *variable_new_c(bridge *cfnc);
struct variable* variable_new_int(int32_t i);
struct variable* variable_new_nil();
int variable_save(const struct variable* v,
				  const struct variable* path);
struct variable *variable_load(const struct variable* path);

uint8_t indent;
#define INDENT indent++;
#define UNDENT indent--;

#else // not DEBUG

#define INDENT
#define UNDENT

#endif // not DEBUG

#define VM_DEBUGPRINT(...) fprintf( stderr, __VA_ARGS__ ); if (!runtime) return;

bool runtime = false;
uint32_t num_vars = 0;
struct variable* error = 0;


// assertions //////////////////////////////////////////////////////////////

jmp_buf trying;

static inline void vm_exit() {
	longjmp(trying, 1);
}

void set_error(const char *format, va_list list)
{
	const char *message = make_message(format, list);
	error = variable_new_err(message);
}

void vm_exit_message(const char *format, ...)
{
	// make error variable
	va_list list;
	va_start(list, format);
	set_error(format, list);
	va_end(list);

	vm_exit();
}

void vm_assert(bool assertion, const char *format, ...)
{
    if (!assertion) {

		// make error variable
		va_list list;
		va_start(list, format);
		set_error(format, list);
		va_end(list);

		vm_exit();
	}
}

void vm_null_check(const void* p) {
    vm_assert(p, "null pointer");
}

// func ////////////////////////////////////////////////////////////////////

bridge *callback2c; // todo: add 'yield' keyword


struct variable *variable_pop()
{
	struct variable *v = lifo_pop(operand_stack);
	//DEBUGPRINT("\nvariable_pop\n");
	//print_operand_stack();
	return v;
}

void variable_push(struct variable *v)
{
	lifo_push(operand_stack, v);
	//DEBUGPRINT("\nvariable_push\n");
	//print_operand_stack();
}


void print(struct lifo *stack)
{
	struct variable *argc = (struct variable*)lifo_pop(stack);
	assert_message(argc->type==VAR_INT && argc->integer==1, "wrong argc passed to print");
	struct variable *v = (struct variable*)lifo_pop(stack);
	printf("%s\n", variable_value(v));
}

void save(struct lifo *stack)
{
	struct variable *argc = (struct variable*)lifo_pop(stack);
	assert_message(argc->type==VAR_INT && argc->integer==2, "wrong argc passed to save");
	struct variable *path = (struct variable*)lifo_pop(stack);
	struct variable *v = (struct variable*)lifo_pop(stack);
	variable_save(v, path);
}

void load(struct lifo *stack)
{
	struct variable *argc = (struct variable*)lifo_pop(stack);
	assert_message(argc->type==VAR_INT && argc->integer==1, "wrong argc passed to load");
	struct variable *path = (struct variable*)lifo_pop(stack);
	struct variable *v = variable_load(path);
	lifo_push(stack, (void*)v);
}

void rm(struct lifo *stack)
{
	struct variable *argc = (struct variable*)lifo_pop(stack);
	assert_message(argc->type==VAR_INT && argc->integer==1, "wrong argc passed to rm");
	struct variable *path = (struct variable*)lifo_pop(stack);
	remove(byte_array_to_string(path->str));
}

void sort(struct lifo *stack)
{
	struct variable *path = (struct variable*)lifo_pop(stack);
	remove(byte_array_to_string(path->str));
}

struct string_func
{
	const char* name;
	bridge* func;
};

struct string_func builtin_funcs[] = {
	{"print", &print},
	{"save", &save},
	{"load", &load},
	{"remove", &rm},
	{"sort", &sort}
};

static inline void func_call(struct byte_array *program)
{
	int32_t num_args = serial_decode_int(program) - 1;
	VM_DEBUGPRINT("CALL %d\n", num_args);
	INDENT

	// get the function pointer from the stack
	struct variable *func = variable_pop();

	// put the passed variable count on the stack
	struct variable *argc = variable_new_int(num_args);
	variable_push(argc);

	// call the function
	switch (func->type) {
		case VAR_FNC:
			run(func->str);
			break;
		case VAR_C:
			func->cfnc(operand_stack);
			break;
		default:
			vm_exit_message("not a function");
			break;
	}

	UNDENT
}

// state ///////////////////////////////////////////////////////////////////

struct program_state {
	struct byte_array *code;
	struct map *named_variables;
	struct array *all_variables;
	uint32_t pc;
};

struct program_state *base_state;

struct program_state *program_state_new(struct byte_array *code)
{
	struct program_state *state = (struct program_state*)malloc(sizeof(struct program_state));
	state->named_variables = map_new();
	state->all_variables = array_new();
	state->code = code;
	byte_array_reset(code);

	if (lifo_empty(program_stack)) {
		base_state = state;
		for (int i=0; i<ARRAY_LEN(builtin_funcs); i++) {
			struct byte_array *name = byte_array_from_string(builtin_funcs[i].name);
			struct variable *v = variable_new_c(builtin_funcs[i].func);
			map_insert(state->named_variables, name, v);
		}
	}
	lifo_push(program_stack, state);
	return state;
}

// variable ////////////////////////////////////////////////////////////////

#define	ERROR_VAR_TYPE	"type error"
#define VAR_MAX	100

const struct number_string var_types[] = {
	{VAR_NIL,		"nil"},
	{VAR_INT,		"integer"},
	{VAR_BOOL,		"boolean"},
	{VAR_FLT,		"float"},
	{VAR_STR,		"string"},
	{VAR_LST,		"list"},
	{VAR_FNC,		"function"},
	{VAR_MAP,		"map"},
	{VAR_ERR,		"error"},
	{VAR_C,			"c-function"},
};

#define FNC_STRING		"string"
#define FNC_LIST		"list"
#define FNC_TYPE		"type"
#define FNC_LENGTH		"length"
#define FNC_KEYS		"keys"
#define FNC_VALUES		"values"
#define FNC_SERIALIZE	"serialize"
#define FNC_DESERIALIZE	"deserialize"
#define FNC_TRY			"try"


struct variable* variable_new(enum VarType type)
{
	if (num_vars++ > VAR_MAX)
		garbage_collect();
	struct variable* v = (struct variable*)malloc(sizeof(struct variable));
	v->type = type;
	v->map = NULL;
	v->marked = false;
	return v;
}

struct variable* variable_new_err(const char* message)
{
	struct variable *v = variable_new(VAR_ERR);
	v->str = byte_array_from_string(message);
	return v;
}

inline struct variable* variable_new_nil()
{
	return variable_new(VAR_NIL);
}

struct variable* variable_new_int(int32_t i)
{
	struct variable *v = variable_new(VAR_INT);
	v->integer = i;
	return v;
}

struct variable* variable_new_bool(bool b)
{
	struct variable *v = variable_new(VAR_BOOL);
	v->boolean = b;
	return v;
}

void variable_del(struct variable *v)
{
	num_vars--;
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
				variable_del((struct variable*)array_get(v->list, i));
			break;
		default:
			vm_exit_message("bad var type");
			break;
	}
	if (v->map) {
		struct array *keys = map_keys(v->map);
		struct array *values = map_values(v->map);
		for (int i=0; i<keys->length; i++) {
			byte_array_del((struct byte_array*)array_get(keys, i));
			variable_del((struct variable*)array_get(values, i));
		}
		array_del(keys);
		array_del(values);
		map_del(v->map);
	}
	free(v);
}

struct variable* variable_new_float(float f)
{
	//DEBUGPRINT("new float %f\n", f);
	struct variable *v = variable_new(VAR_FLT);
	v->floater = f;
	return v;
}

struct variable *variable_new_str(struct byte_array *str) {
	struct variable *v = variable_new(VAR_STR);
	v->str = str;
	return v;
}

struct variable *variable_new_fnc(struct byte_array *fnc) {
	struct variable *v = variable_new(VAR_FNC);
	v->str = fnc;
	return v;
}

struct variable *variable_new_list(struct array *list) {
	struct variable *v = variable_new(VAR_LST);
	v->list = list;
	return v;
}

struct variable *variable_new_map(struct map *map) {
	struct variable *v = variable_new(VAR_MAP);
	v->map = map;
	return v;
}

struct variable *variable_new_c(bridge *cfnc) {
	struct variable *v = variable_new(VAR_C);
	v->cfnc = cfnc;
	return v;
}

const char *variable_value(const struct variable* v)
{
	char* str = (char*)malloc(100);
	enum VarType vt = (enum VarType)v->type;
	switch (vt) {
		case VAR_NIL:	sprintf(str, "nil");									break;
		case VAR_INT:	sprintf(str, "%d", v->integer);							break;
		case VAR_BOOL:	sprintf(str, "%s", v->boolean ? "true" : "false");		break;
		case VAR_FLT:	sprintf(str, "%f", v->floater);							break;
		case VAR_STR:	sprintf(str, "%s", byte_array_to_string(v->str));		break;
		case VAR_FNC:	sprintf(str, "f(%dbytes)", v->str->size);				break;
		case VAR_C:		sprintf(str, "c function");								break;
		case VAR_MAP:															break;
		case VAR_LST: {
			strcpy(str, "[");
			struct array* list = v->list;
			vm_null_check(list);
			for (int i=0; i<list->length; i++) {
				struct variable* element = (struct variable*)array_get(list, i);
				vm_null_check(element);
				const char *q = (element->type == VAR_STR || element->type == VAR_FNC) ? "'" : "";
				const char *c = i ? "," : "";
				sprintf(str, "%s%s%s%s%s", str, c, q, variable_value(element), q);
			}
		} break;
		default:		vm_exit_message(ERROR_VAR_TYPE);							break;
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
			strcat(str, variable_value((const struct variable*)array_get(b,i)));
		}
		strcat(str, "]");
	}
	else if (vt == VAR_LST)
		strcat(str, "]");
	
	return str;
}

// garbage collection //////////////////////////////////////////////////////

void mark(struct variable *root)
{
	if (root->map) {
		const struct array *values = map_values(root->map);
		for (int i=0; values && i<values->length; i++)
			mark((struct variable*)array_get(values, i));
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
				mark((struct variable*)array_get(root->list, i));
			break;
		default:
			vm_exit_message("bad var type");
			break;
	}
}

void sweep(struct variable *root)
{
	struct program_state *state = (struct program_state*)lifo_peek(program_stack, 0);
	struct array *vars = state->all_variables; 
	for (int i=0; i<vars->length; i++) {
		struct variable *v = (struct variable*)array_get(vars, i);
		if (!v->marked)
			variable_del(v);
		else
			v->marked = false;
	}
}

void garbage_collect()
{
	struct program_state *state = (struct program_state*)lifo_peek(program_stack, 0);
	struct array *vars = state->all_variables; 
	for (int i=0; i<vars->length; i++) {
		struct variable *v = (struct variable*)array_get(vars, i);
		mark(v);
		sweep(v);
	}
}

// instruction implementations /////////////////////////////////////////////

void push_list(struct byte_array *program)
{
	int32_t num_items = serial_decode_int(program);
	DEBUGPRINT("LST %d", num_items);
	if (!runtime)
		VM_DEBUGPRINT("\n");
	struct array *items = array_new();
	struct map *map = map_new(); 

	while (num_items--) {
		struct variable* v = variable_pop();
		if (v->type == VAR_MAP)
			map_update(map, v->map); // mapped values are stored in the map, not list
		else
			array_insert(items, 0, v);
	}
	struct variable *list = variable_new_list(items);
	list->map = map;
	DEBUGPRINT(": %s\n", variable_value(list));
	variable_push(list);
}

void push_map(struct byte_array *program)
{
	int32_t num_items = serial_decode_int(program) / 2;
	DEBUGPRINT("MAP %d", num_items);
	if (!runtime)
		VM_DEBUGPRINT("\n");
	struct map *map = map_new();
	while (num_items--) {
		struct variable* key = variable_pop();;
		struct variable* value = variable_pop();;
		map_insert(map, key->str, value);
	}
	struct variable *v = variable_new_map(map);
	DEBUGPRINT(": %s\n", variable_value(v));
	variable_push(v);
}

struct variable* variable_set(struct variable *u, const struct variable* v)
{
	vm_null_check(u);
	vm_null_check(v);
	switch (v->type) {
		case VAR_NIL:										break;
		case VAR_INT:	u->integer = v->integer;			break;
		case VAR_FLT:	u->floater = v->floater;			break;
		case VAR_FNC:
		case VAR_STR:	u->str = byte_array_copy(v->str);	break;
		case VAR_LST:	u->list = v->list;
						u->list->current = u->list->data;	break;
		default:		vm_exit_message("bad var type");		break;
	}
	if (v->type == VAR_STR)
		u->str = byte_array_copy(v->str);
	u->map = v->map;
	return u;
}

struct variable* variable_copy(const struct variable* v)
{
	vm_null_check(v);
	struct variable *u = variable_new((enum VarType)v->type);
	variable_set(u, v);
	return u;
}

// display /////////////////////////////////////////////////////////////////

#ifdef DEBUG

const struct number_string opcodes[] = {
	{VM_NIL,		"NIL"},
	{VM_INT,		"INT"},
	{VM_BOOL,		"BOOL"},
	{VM_FLT,		"FLT"},
	{VM_STR,		"STR"},
	{VM_VAR,		"VAR"},
	{VM_FNC,		"FNC"},
	{VM_SET,		"SET"},
	{VM_LST,		"LST"},
	{VM_MAP,		"MAP"},
	{VM_GET,		"GET"},
	{VM_PUT,		"PUT"},
	{VM_ADD,		"ADD"},
	{VM_SUB,		"SUB"},
	{VM_MUL,		"MUL"},
	{VM_DIV,		"DIV"},
	{VM_AND,		"AND"},
	{VM_OR,			"OR "},
	{VM_NOT,		"NOT"},
	{VM_NEG,		"NEG"},
	{VM_EQU,		"EQ "},
	{VM_NEQ,		"NEQ"},
	{VM_GT,			"GT "},
	{VM_LT,			"LT "},
	{VM_IF,			"IF "},
	{VM_JMP,		"JMP"},
	{VM_CAL,		"CALL"},
	{VM_ARG,		"ARG"},
	{VM_RET,		"RET"},
};

void print_operand_stack()
{
	struct variable *operand;
	for (int i=0; (operand = lifo_peek(operand_stack, i)); i++)
		DEBUGPRINT("\t%s\n", variable_value(operand));
}

const char* indentation()
{
	static char str[100];
	int tab = 0;
	while (tab < indent)
		str[tab++] = '\t';
	str[tab] = 0;
	return (const char*)str;
}

static inline void display_program_counter(const struct byte_array *program)
{
	DEBUGPRINT("%s%2ld:%3d ", indentation(), program->current-program->data, *program->current);
}

void display_code(struct byte_array *code)
{
	bool was_running = runtime;
	runtime = false;

	INDENT
	run(code);
	UNDENT

	runtime = was_running;
}

void display_program(const char* title, struct byte_array *program)
{
	title = title ? title : "program";
	
	INDENT
	DEBUGPRINT("%s%s bytes:\n", indentation(), title);

	INDENT
	for (int i=0; i<program->size; i++)
		DEBUGPRINT("%s%2d:%3d\n", indentation(), i, program->data[i]);

	DEBUGPRINT("%s%s instructions:\n", indentation(), title);
	byte_array_reset(program);
	struct byte_array* code = serial_decode_string(program);

	program_stack = lifo_new();
	operand_stack = lifo_new();
	
	display_code(code);

	UNDENT
	UNDENT
}

#endif // DEBUG


// run /////////////////////////////////////////////////////////////////////

struct byte_array *variable_serialize(struct byte_array *bits,
									  const struct variable *in)
{
	//DEBUGPRINT("\tserialize:%s\n", variable_value(in));
	if (!bits)
		bits = byte_array_new();
	serial_encode_int(bits, 0, in->type);
	switch (in->type) {
		case VAR_INT:	serial_encode_int(bits, 0, in->integer);	break;
		case VAR_FLT:	serial_encode_float(bits, 0, in->floater);	break;
		case VAR_STR:
		case VAR_FNC:	serial_encode_string(bits, 0, in->str);		break;
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
		case VAR_MAP:												break;
		default:		vm_exit_message("bad var type");				break;
	}

	//DEBUGPRINT("in: %s\n", variable_value(in));
	//byte_array_print("serialized: ", bits);
	return bits;
}

struct variable *variable_deserialize(struct byte_array *bits)
{
	enum VarType vt = (enum VarType)serial_decode_int(bits);
	switch (vt) {
		case VAR_NIL:	return variable_new_nil();
		case VAR_INT:	return variable_new_int(serial_decode_int(bits));
		case VAR_FLT:	return variable_new_float(serial_decode_float(bits));
		case VAR_FNC:	return variable_new_fnc(serial_decode_string(bits));
		case VAR_STR:	return variable_new_str(serial_decode_string(bits));
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

int variable_save(const struct variable* v,
				  const struct variable* path)
{
	vm_null_check(v);
	vm_null_check(path);

	struct byte_array *bytes = byte_array_new();
	variable_serialize(bytes, v);
	return write_file(path->str, bytes);
}

struct variable *variable_load(const struct variable* path)
{
	vm_null_check(path);
	
	struct byte_array *file_bytes = read_file(path->str);
	struct variable *v = variable_deserialize(file_bytes);
	return v;
}

static inline struct variable *builtin_method(const struct variable *indexable,
											  const struct variable *index)
{
	const char *idxstr = byte_array_to_string(index->str);
	if (!strcmp(idxstr, FNC_LENGTH))
		return variable_new_int(indexable->list->length);
	if (!strcmp(idxstr, FNC_TYPE)) {
		const char *typestr = NUM_TO_STRING(var_types,
											indexable->type);
		return variable_new_str(byte_array_from_string(typestr));
	}
	else if (!strcmp(idxstr, FNC_STRING))
		return variable_new_str(byte_array_from_string(variable_value(indexable)));
	else if (!strcmp(idxstr, FNC_LIST))
		return variable_new_list(indexable->list);
	else if (!strcmp(idxstr, FNC_KEYS)) {
		struct variable *v = variable_new_list(array_new());
		if (indexable->map) {
			const struct array *a = map_keys(indexable->map);
			for (int i=0; i<a->length; i++) {
				struct variable *u = variable_new_str((struct byte_array*)array_get(a, i));
				array_add(v->list, u);
			}
		}
		return v;
	} else if (!strcmp(idxstr, FNC_VALUES)) {
		if (!indexable->map)
			return variable_new_list(array_new());
		else return variable_new_list((struct array*)map_values(indexable->map));
	} else if (!strcmp(idxstr, FNC_SERIALIZE)) {
		struct byte_array *bits = variable_serialize(0, indexable);
		return variable_new_str(bits);
	} else if (!strcmp(idxstr, FNC_DESERIALIZE)) {
		struct byte_array *bits = indexable->str;
		byte_array_reset(bits);
		struct variable *d = variable_deserialize(bits);
		return d;
	} else 
		return NULL;
}

static inline struct variable *list_get_int(const struct variable *indexable,
											const struct variable *index)
{
	uint32_t n = index->integer;
	enum VarType it = (enum VarType)indexable->type;
	switch (it) {
		case VAR_LST:
			return (struct variable*)array_get(indexable->list, n);
		case VAR_STR: {
			vm_assert(n<indexable->str->size, "index out of bounds");
			char *str = (char*)malloc(2);
			sprintf(str, "%c", indexable->str->data[n]);
			return variable_new_str(byte_array_from_string(str));
		}
		default:
			vm_exit_message("indexing non-indexable");
			return NULL;
	}
}

static inline void list_get()
{
	if (!runtime)
		VM_DEBUGPRINT("GET\n");
	
	struct variable *indexable, *index, *item=0;
	indexable = variable_pop();;
	index = variable_pop();;
	
	switch (index->type) {
		case VAR_INT:
			item = list_get_int(indexable, index);
			break;
		case VAR_STR:
			if (indexable->map)
				item = (struct variable*)map_get(indexable->map, index->str);
			if (!item)
				item = builtin_method(indexable, index);
			vm_assert(item, "did not find member");
			break;
		default:
			vm_exit_message("bad index type");
			break;
	}
	DEBUGPRINT("GET %s\n", variable_value(item));
	variable_push(item);
}

int32_t jump(struct byte_array *program)
{
	uint8_t *start = program->current;
	int32_t offset = serial_decode_int(program);
	DEBUGPRINT("JMP %d\n", offset);
	if (!runtime)
		return 0;

	return offset - (program->current - start);
}

static inline int32_t iff(struct byte_array *program)
{
	int32_t offset = serial_decode_int(program);
	DEBUGPRINT("IF %d\n", offset);
	if (!runtime)
		return 0;
	struct variable* v = variable_pop();;
	bool go = false;
	switch (v->type) {
		case VAR_NIL:	go = false;							break;
		case VAR_BOOL:	go = v->boolean;					break;
		case VAR_INT:	go = v->integer;					break;			
		default:		vm_exit_message("bad iff operand");	break;
	}
	return go ? 0 : (VOID_INT)offset;
}

static inline void push_nil()
{
	struct variable* var = variable_new_nil();
	VM_DEBUGPRINT("NIL\n");
	variable_push(var);
}

static inline void push_int(struct byte_array *program)
{
	int32_t num = serial_decode_int(program);
	VM_DEBUGPRINT("INT %d\n", num);
	struct variable* var = variable_new_int(num);
	variable_push(var);
}

static inline void push_bool(struct byte_array *program)
{
	int32_t num = serial_decode_int(program);
	VM_DEBUGPRINT("BOOL %d\n", num);
	struct variable* var = variable_new_bool(num);
	variable_push(var);
}

static inline void push_float(struct byte_array *program)
{
	float num = serial_decode_float(program);
	VM_DEBUGPRINT("FLT %f\n", num);
	struct variable* var = variable_new_float(num);
	variable_push(var);
}

struct variable *find_var_in_stack(const struct program_state *state,
								   const struct byte_array *name)
{
	struct map *var_map = state->named_variables;
	return (struct variable*)(struct variable*)map_get(var_map, name);
}

struct variable *find_var(const struct program_state *state,
						  const struct byte_array *name)
{
	struct variable *v = find_var_in_stack(state, name);
	if (!v)
		v = find_var_in_stack(base_state, name); // todo: dynamic scoping
	return v;
}

static inline void push_var(struct program_state *state)
{	
	struct byte_array *program = state->code;
	struct byte_array* name = serial_decode_string(program);
	VM_DEBUGPRINT("VAR %s\n", byte_array_to_string(name));
	struct variable *v = find_var(state, name);
	vm_assert(v, "variable not found");
	variable_push((void*)v);
}

static inline void push_str(struct byte_array *program)
{
	struct byte_array* str = serial_decode_string(program);
	VM_DEBUGPRINT("STR '%s'\n", byte_array_to_string(str));
	struct variable* v = variable_new_str(str);
	variable_push((void*)v);
}

static inline void push_fnc(struct byte_array *program)
{
	uint32_t fcodelen = serial_decode_int(program);
	struct byte_array* fbody = byte_array_new_size(fcodelen);
	memcpy(fbody->data, program->current, fcodelen);

	DEBUGPRINT("FNC\n");
	display_code(fbody);

	if (runtime) {
		struct variable* var = variable_new_fnc((struct byte_array*)fbody);
		variable_push((void*)var);
	}
	program->current += fcodelen;
}

void set_named_variable(struct program_state *state,
						const struct byte_array *name,
						const struct variable *value)
{
	struct map *var_map = state->named_variables;
	struct variable *to_var = find_var(state, name);

	if (!to_var) { // new variable
		to_var = variable_copy(value);
		to_var->name = byte_array_copy(name);
	} else
		variable_set(to_var, value);

	map_insert(var_map, name, (void*)to_var);

//	DEBUGPRINT(" (SET %s to %s)", byte_array_to_string(name), variable_value(to_var));	
}

static inline void set(struct program_state *state)
{
	const struct byte_array* name = serial_decode_string(state->code);
	VM_DEBUGPRINT("SET %s\n", byte_array_to_string(name));
	const struct variable* value = variable_pop();;
	set_named_variable(state, name, value);
}

static inline void arg(struct program_state *state)
{
	int32_t num_parameters;
	struct array *parameters;
	if (runtime) {
		// get parameters from function call
		struct variable *popped = variable_pop();
		vm_assert(popped->type == VM_INT, "don't know how many parameters to pop");
		num_parameters = popped->integer;
		parameters = array_new_size(num_parameters);
		for (int i=0; i<num_parameters; i++)
			array_set(parameters, i, variable_pop());
	}

	// set arguments from function declaration
	struct byte_array *program = state->code;
	int32_t num_arguments = serial_decode_int(program);
	DEBUGPRINT("ARG %d", num_arguments);
	for (int j=0; j<num_arguments; j++) {
		struct byte_array *name = serial_decode_string(program);// argument name
		struct variable *value = variable_deserialize(program); // default value
		DEBUGPRINT(" %s:%s", byte_array_to_string(name), variable_value(value));
		if (!runtime)
			continue;
		if (j + num_parameters >= num_arguments) { // a value was passed in the function call
			value = array_get(parameters, j - (num_arguments - num_parameters));
			DEBUGPRINT(":%s", variable_value(value));
		}
		set_named_variable(state, name, value); // set the argument
	}
	DEBUGPRINT("\n");
}

static inline void list_put()
{
	DEBUGPRINT("PUT");
	if (!runtime)
		VM_DEBUGPRINT("\n");
	struct variable* recipient = variable_pop();;
	struct variable* key = variable_pop();
	struct variable* value = (struct variable*)variable_pop();;
	
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
					vm_exit_message("indexing non-indexable");
			} break;
		case VAR_STR:
			if (!recipient->map)
				recipient->map = map_new();
			map_insert(recipient->map, key->str, value);
			break;
		default:
			vm_exit_message("bad index type");
			break;
	}
	DEBUGPRINT(": %s\n", variable_value(recipient));
}

static inline struct variable *binary_op_int(enum Opcode op,
											 const struct variable *u,
											 const struct variable *v)
{
	int32_t m = u->integer;
	int32_t n = v->integer;
	int32_t i;
	switch (op) {
		case VM_MUL:	i = m * n;	break;
		case VM_DIV:	i = m / n;	break;
		case VM_ADD:	i = m + n;	break;
		case VM_SUB:	i = m - n;	break;
		case VM_AND:	i = m && n;	break;
		case VM_EQU:		i = m == n;	break;
		case VM_OR:		i = m || n;	break;
		case VM_GT:		i = n > m;	break;
		case VM_LT:		i = n < m;	break;
		default:
			vm_exit_message("bad math int operator");
			return NULL;
	}
	return variable_new_int(i);
}

static inline struct variable *binary_op_float(enum Opcode op,
											   const struct variable *u,
											   const struct variable *v)
{
	float m = u->floater;
	float n = v->floater;
	float f = 0;
	switch (op) {
		case VM_MUL:	f = m * n;							break;
		case VM_DIV:	f = m / n;							break;
		case VM_ADD:	f = m + n;							break;
		case VM_SUB:	f = m - n;							break;
		case VM_NEQ:	f = m != n;							break;
		case VM_GT:		return variable_new_int(n > m);
		case VM_LT:		return variable_new_int(n < m);
		default:
			vm_exit_message("bad math float operator");
			return NULL;
	}
	return variable_new_float(f);
}

static inline bool is_num(enum VarType vt) {
	return vt == VAR_INT || vt == VAR_FLT;
}

static inline struct variable *binary_op_str(enum Opcode op,
											 const struct variable *u,
											 const struct variable *v)
{
	struct variable *w = NULL;
	struct byte_array *ustr = byte_array_from_string(variable_value(u));
	struct byte_array *vstr = byte_array_from_string(variable_value(v));
	
	switch (op) {
		case VM_ADD:	w = variable_new_str(byte_array_concatenate(2, vstr, ustr));	break;
		case VM_EQU:	w = variable_new_int(byte_array_equals(ustr, vstr));			break;
		default:		vm_exit_message("unknown string operation");						break;
	}
	return w;
}

static inline bool variable_compare(const struct variable *u,
									const struct variable *v)
{
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
				if (!variable_compare(ui, vi))
					return false;		
			}
			// for list, check the map too
		case VAR_MAP: {
			struct array *keys = map_keys(u->map);
			for (int i=0; i<keys->length; i++) {
				struct byte_array *key = array_get(keys, i);
				struct variable *uvalue = map_get(u->map, key);
				struct variable *vvalue = map_get(v->map, key);
				if (!variable_compare(uvalue, vvalue))
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
			vm_exit_message("bad comparison");
			return false;
	}
}

static inline struct variable *binary_op_lst(enum Opcode op,
											 const struct variable *u,
											 const struct variable *v)
{
	vm_assert(u->type==VAR_LST && v->type==VAR_LST, "list op with non-lists");
	struct variable *w = NULL;

	switch (op) {
		case VM_ADD:
			w = variable_copy(v);
			for (int i=0; i<u->list->length; i++)
				array_add(w->list, array_get(u->list, i));
			map_update(w->map, u->map);
			break;
		default:
			vm_exit_message("unknown string operation");
			break;
	}

	return w;
}

static inline void binary_op(enum Opcode op)
{
	if (!runtime)
		VM_DEBUGPRINT("%s\n", NUM_TO_STRING(opcodes, op));

	const struct variable *u = variable_pop();
	const struct variable *v = variable_pop();
	struct variable *w;

	if (op == VM_EQU) {
		bool same = variable_compare(u, v);
		w = variable_new_int(same);
	} else {
	
		enum VarType ut = (enum VarType)u->type;
		enum VarType vt = (enum VarType)v->type;
		bool floater  = (ut == VAR_FLT && is_num(vt)) || (vt == VAR_FLT && is_num(ut));

		if (vt == VAR_STR || ut == VAR_STR)			w = binary_op_str(op, u, v);
		else if (floater)							w = binary_op_float(op, u, v);
		else if (ut == VAR_INT && vt == VAR_INT)	w = binary_op_int(op, u, v);
		else if (vt == VAR_LST)						w = binary_op_lst(op, u, v);
		else
			vm_exit_message("unknown binary op");
	}
	
	variable_push(w);

	DEBUGPRINT("%s(%s,%s) = %s\n",
			   NUM_TO_STRING(opcodes, op),
			   variable_value(v),
			   variable_value(u),
			   variable_value(w));	
}

static inline void unary_op(enum Opcode op)
{
	if (!runtime)
		VM_DEBUGPRINT("%s\n", NUM_TO_STRING(opcodes, op));

	struct variable *v = (struct variable*)variable_pop();;
	struct variable *result = NULL;

	switch (v->type) {
		case VAR_NIL:
		{
			switch (op) {
				case VM_NEG:	result = variable_new_nil();		break;
				case VM_NOT:	result = variable_new_bool(true);	break;
				default:		vm_exit_message("bad math operator");	break;
			}
		} break;
		case VAR_INT: {
			int32_t n = v->integer;
			switch (op) {
				case VM_NEG:	result = variable_new_int(-n);		break;
				case VM_NOT:	result = variable_new_bool(!n);		break;
				default:		vm_exit_message("bad math operator");	break;
			}
		} break;
		default:	vm_exit_message("bad math type");	break;
	}

	variable_push(result);
	
	DEBUGPRINT("%s(%s) = %s\n",
			   NUM_TO_STRING(opcodes, op),
			   variable_value(v),
			   variable_value(result));
}

struct variable* run(struct byte_array *program)
{
	struct program_state *state = program_state_new(program);

	while (program->current < program->data + program->size) {
		enum Opcode inst = (enum Opcode)*program->current;
#ifdef DEBUG
		display_program_counter(program);
#endif
		program->current++;
		num_inst_executed++;

		if (inst == VM_RET) {
			DEBUGPRINT("RET\n");
			break;
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
			case VM_GT:
			case VM_LT:
			case VM_OR:		binary_op(inst);				break;
			case VM_NEG:	
			case VM_NOT:	unary_op(inst);					break;
			case VM_SET:	set(state);						break;
			case VM_JMP:	pc_offset = jump(program);		break;
			case VM_IF:		pc_offset = iff(program);		break;
			case VM_CAL:	func_call(program);				break;
			case VM_LST:	push_list(program);				break;
			case VM_MAP:	push_map(program);				break;
			case VM_GET:	list_get();						break;
			case VM_PUT:	list_put();						break;
			case VM_NIL:	push_nil();						break;
			case VM_INT:	push_int(program);				break;
			case VM_FLT:	push_float(program);			break;
			case VM_BOOL:	push_bool(program);				break;
			case VM_STR:	push_str(program);				break;
			case VM_VAR:	push_var(state);				break;
			case VM_FNC:	push_fnc(program);				break;
			case VM_ARG:	arg(state);						break;
			default:		vm_exit_message(ERROR_OPCODE);	break;
		}

		program->current += pc_offset;
	}

	lifo_pop(program_stack);
	return (struct variable*)lifo_peek(operand_stack, 0);
}

struct variable *execute(struct byte_array *program, bridge *callback_to_c)
{
	DEBUGPRINT("execute:\n");
	callback2c = callback_to_c;
	vm_assert(program!=0 && program->data!=0, ERROR_NULL);
	byte_array_reset(program);
	struct byte_array* code = serial_decode_string(program);

	program_stack = lifo_new();
	operand_stack = lifo_new();

	runtime = true;
	num_vars = 0;
#ifdef DEBUG
	indent = 1;
#endif
	
	clock_t start, end;
	double elapsed;
	start = clock();

	struct variable *v;
	if (!setjmp(trying))
		v = run(code);
	else
		v = error;		

	end = clock();
	elapsed = ((double) (end - start)) * 1000 / CLOCKS_PER_SEC;
	printf("%u instructions took %fms: %f instructions per ms\n", num_inst_executed, elapsed, num_inst_executed/elapsed);

	return v;
}
