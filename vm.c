#include <stdio.h>
#include <signal.h>
#include <string.h>
#include "util.h"
#include "serial.h"
#include <time.h>
#include "vm.h"


void garbage_collect();
struct ifo *program_stack;
struct ifo *operand_stack;
struct variable *variable_new_c(bridge *cfnc);
uint32_t num_inst_executed = 0;

#ifdef DEBUG

void display_instruction(struct byte_array *program);
struct variable* run(struct byte_array *program, bool in_context);
struct variable* variable_new_err(const char* message);

uint8_t indent;
#define INDENT indent++
#define UNDENT indent--

#else

#define INDENT
#define UNDENT

#endif

// file-level variables
struct variable* run(struct byte_array *program, bool in_context);
bool runtime = false;
uint32_t num_vars = 0;
struct variable* error = 0;


// assertions //////////////////////////////////////////////////////////////

jmp_buf trying;

void exit_message(const char *message) {
    if (message)
        DEBUGPRINT("%s\n", message);
	error = variable_new_err(message ? message : "error");
	longjmp(trying, 1);
}

void assert_message(bool assertion, const char* message) {
    if (!assertion)
        exit_message(message);
}

void null_check(const void* p) {
    assert_message(p, "null pointer");
}

// func ////////////////////////////////////////////////////////////////////

bridge *callback2c; // todo: add 'yield' keyword

void print(struct ifo *stack)
{
	struct variable *v = (struct variable*)ifo_pop(stack);
	printf("%s\n", variable_value(v));
}

struct string_func
{
	const char* name;
	bridge* func;
};

struct string_func builtin_funcs[] = {
	{"print", &print},
};

static inline void func_call()
{
	DEBUGPRINT("CALL\n");
	struct variable *func;
	
	func = (struct variable*)(struct variable*)ifo_pop(operand_stack);
	switch (func->type) {
		case VAR_FNC:
			run(func->str, false);
			break;
		case VAR_C:
			func->cfnc(operand_stack);
			break;
		default:
			exit_message("not a function");
			break;
	}
}

// state ///////////////////////////////////////////////////////////////////

struct program_state {
	struct byte_array *code;
	struct map *named_variables;
	struct array *all_variables;
	uint32_t pc;
};

struct program_state *base_state;

struct program_state *program_state_new()
{
	struct program_state *state = (struct program_state*)malloc(sizeof(struct program_state));
	state->named_variables = map_new();
	state->all_variables = array_new();
	state->code = NULL;
	if (ifo_empty(program_stack)) {
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

struct program_state *program_state_from_code(struct byte_array *code)
{
	struct program_state *state = program_state_new();
	state->code = code;
	return state;
}

// variable ////////////////////////////////////////////////////////////////

#define	ERROR_VAR_TYPE	"type error"
#define VAR_MAX	100

const struct number_string var_types[] = {
	{VAR_INT,		"integer"},
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

struct variable* variable_new_int(int32_t i)
{
	struct variable *v = variable_new(VAR_INT);
	v->integer = i;
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
			exit_message("bad var type");
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
		case VAR_INT:	sprintf(str, "%d", v->integer);							break;
		case VAR_FLT:	sprintf(str, "%f", v->floater);							break;
		case VAR_STR:	sprintf(str, "%s", byte_array_to_string(v->str));		break;
		case VAR_FNC:	sprintf(str, "function #%d", v->str->size);				break;
		case VAR_MAP:															break;
		default:		exit_message(ERROR_VAR_TYPE);							break;
		case VAR_LST: {
			strcpy(str, "[");
			struct array* list = v->list;
			for (int i=0; i<list->length; i++) {
				struct variable* element = (struct variable*)array_get(list, i);
				const char *q = (element->type == VAR_STR || element->type == VAR_FNC) ? "'" : "";
				const char *c = i ? "," : "";
				sprintf(str, "%s%s%s%s%s", str, c, q, variable_value(element), q);
			}
		} break;
	}
	
	if (v->map) {
		if (vt != VAR_LST)
			strcat(str, "[");
		else if (v->list->length)
			strcat(str, ",");
		const struct array *a = map_keys(v->map);
		const struct array *b = map_values(v->map);
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
			exit_message("bad var type");
			break;
	}
}

void sweep(struct variable *root)
{
	struct program_state *state = (struct program_state*)ifo_peek(program_stack, 0);
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
	struct program_state *state = (struct program_state*)ifo_peek(program_stack, 0);
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
	DEBUGPRINT("LST %d\n", num_items);
	struct array *items = array_new_size(num_items);
	struct map *map = map_new(); 
	
	while (num_items--) {
		struct variable* v = (struct variable*)ifo_pop(operand_stack);
		if (v->type == VAR_MAP)
			map_union(map, v->map);
		else
			array_set(items, num_items, v);
	}
	struct variable *list = variable_new_list(items);
	list->map = map;
	lifo_push(operand_stack, list);
}

void push_map(struct byte_array *program)
{
	int32_t num_items = serial_decode_int(program);
	DEBUGPRINT("MAP %d\n", num_items);
	struct map *map = map_new();
	while (num_items--) {
		struct variable* key = (struct variable*)ifo_pop(operand_stack);
		struct variable* value = (struct variable*)ifo_pop(operand_stack);
		map_insert(map, key->str, value);
	}
	struct variable *v = variable_new_map(map);
	lifo_push(operand_stack, v);
}

struct variable* variable_set(struct variable *u, const struct variable* v)
{
	null_check(u);
	null_check(v);
	switch (v->type) {
		case VAR_INT:	u->integer = v->integer;			break;
		case VAR_FLT:	u->floater = v->floater;			break;
		case VAR_FNC:
		case VAR_STR:	u->str = byte_array_copy(v->str);	break;
		case VAR_LST:	u->list = v->list;					break;
		default:		exit_message("bad var type");		break;
	}
	if (v->type == VAR_STR)
		u->str = byte_array_copy(v->str);
	u->map = v->map;
	return u;
}

struct variable* variable_copy(const struct variable* v)
{
	null_check(v);
	struct variable *u = variable_new((enum VarType)v->type);
	variable_set(u, v);
	return u;
}

// display /////////////////////////////////////////////////////////////////

#ifdef DEBUG

const struct number_string opcodes[] = {
	{VM_SET,		"SET"},
	{VM_INT,		"INT"},
	{VM_FLT,		"FLT"},
	{VM_STR,		"STR"},
	{VM_VAR,		"VAR"},
	{VM_FNC,		"FNC"},
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
	{VM_NOT,		"NOT "},
	{VM_NEG,		"NEG"},
	{VM_EQ,			"EQ "},
	{VM_NEQ,		"NEQ "},
	{VM_GT,			"GT "},
	{VM_LT,			"LT "},
	{VM_IF,			"IF "},
	{VM_JMP,		"JMP"},
	{VM_CAL,		"CAL"},
};

// todo: remove
static inline void display_instruction_pushint(struct byte_array *program, enum Opcode op)
{
	int32_t num = serial_decode_int(program);
	DEBUGPRINT("%s %d\n", num_to_string(opcodes, ARRAY_LEN(opcodes), op), num);
}

static inline void display_instruction_pushflt(struct byte_array *program, enum Opcode op)
{
	float num = serial_decode_float(program);
	DEBUGPRINT("%s %f\n", num_to_string(opcodes, ARRAY_LEN(opcodes), op), num);
}

static inline void display_instruction_string(struct byte_array *program, enum Opcode op)
{
	const struct byte_array* str = serial_decode_string(program);
	DEBUGPRINT("%s %s\n", num_to_string(opcodes, ARRAY_LEN(opcodes), op), byte_array_to_string(str));
}

static inline void display_instruction_pushfnc(struct byte_array *program)
{
	char pf[100] = "PUSHFNC ";
	
	uint8_t* start = program->current;
	struct byte_array *code = serial_decode_string(program);
	sprintf(pf, "%s #%d", pf, code->size);
	DEBUGPRINT("%s\n", pf);
	if (runtime)
		return;
	INDENT;
	while (program->current < start + code->size)
		display_instruction(program);
	UNDENT;
}

static inline void display_instruction_itr(struct byte_array *program)
{
	struct byte_array *name = serial_decode_string(program);
	struct byte_array *table_code = serial_decode_string(program);
	struct byte_array *clause_code = serial_decode_string(program);
	struct byte_array *item_code = serial_decode_string(program);
	DEBUGPRINT("VM_ITR %s\n", byte_array_to_string(name));
	display_program("table_code", table_code);
	display_program("clause_code", clause_code);
	display_program("item_code", item_code);
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
	DEBUGPRINT("%s%2ld:%3d\t", indentation(), program->current-program->data, *program->current);
}

void display_instruction(struct byte_array *program)
{
	uint8_t* nowwherewasi = program->current;
	
	display_program_counter(program);
	enum Opcode inst = *program->current;
	program->current++;
	switch (inst) {
		case VM_SET:
		case VM_STR:
		case VM_VAR:	display_instruction_string(program, inst);								break;
		case VM_IF:
		case VM_JMP:
		case VM_LST:
		case VM_MAP:
		case VM_INT:	display_instruction_pushint(program, inst);								break;
		case VM_FLT:	display_instruction_pushflt(program, inst);								break;
		case VM_FNC:	display_instruction_pushfnc(program);									break;
		default:		DEBUGPRINT("%s\n", num_to_string(opcodes, ARRAY_LEN(opcodes), inst));	break;
	}
	if (runtime)
		program->current = nowwherewasi; // don't advance the program counter when executing
}

void display_program(const char* title, struct byte_array *program)
{
	title = title ? title : "program";
	INDENT;
	DEBUGPRINT("%s%s bytes:\n", indentation(), title);
	INDENT;
	for (int i=0; i<program->size; i++)
		printf("%s%2d:%3d\n", indentation(), i, program->data[i]);
	DEBUGPRINT("%s%s instructions:\n", indentation(), title);
	runtime = false;
	INDENT;
	while (program->current < program->data + program->size)
		display_instruction(program);
	UNDENT;UNDENT;UNDENT;
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
		} break;
		case VAR_MAP:												break;
		default:		exit_message("bad var type");				break;
	}
	
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
	
	//	byte_array_print("serialized:", bits);
	return bits;
}

struct variable *variable_deserialize(struct byte_array *bits)
{
	//	byte_array_print("deserialized:", bits);
	enum VarType vt = (enum VarType)serial_decode_int(bits);
	struct variable *out;
	
	switch (vt) {
		case VAR_INT:	out = variable_new_int(serial_decode_int(bits));		break;
		case VAR_FLT:	out = variable_new_float(serial_decode_float(bits));	break;
		case VAR_FNC:	out = variable_new_fnc(serial_decode_string(bits));		break;
		case VAR_STR:	out = variable_new_str(serial_decode_string(bits));		break;
		case VAR_LST: {
			uint32_t size = serial_decode_int(bits);
			struct array *list = array_new_size(size);
			while (size--)
				array_add(list, variable_deserialize(bits));
			out = variable_new_list(list);
		} break;
		default:
			exit_message("bad var type");
			return NULL;
	}
	
	uint32_t map_length = serial_decode_int(bits);
	if (map_length) {
		out->map = map_new();
		for (int i=0; i<map_length; i++) {
			struct byte_array *key = serial_decode_string(bits);
			struct variable *value = variable_deserialize(bits);
			map_insert(out->map, key, value);
		}
	}
	
	//DEBUGPRINT("\tdeserialized:%s\n", variable_value(out));
	return out;
}

static inline struct variable *default_member(const struct variable *indexable,
											  const struct variable *index)
{
	const char *idxstr = byte_array_to_string(index->str);
	if (!strcmp(idxstr, FNC_LENGTH))
		return variable_new_int(indexable->list->length);
	if (!strcmp(idxstr, FNC_TYPE)) {
		const char *typestr = num_to_string(var_types,
											ARRAY_LEN(var_types),
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

const char* num_to_string(const struct number_string *ns, int num_items, int num)
{
	for (int i=0; i<num_items; i++) // reverse lookup nonterminal string
		if (num == ns[i].number)
			return ns[i].chars;
	exit_message("num not found");
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
			assert_message(n<indexable->str->size, "index out of bounds");
			char *str = (char*)malloc(2);
			sprintf(str, "%c", indexable->str->data[n]);
			return variable_new_str(byte_array_from_string(str));
		}
		default:
			exit_message("indexing non-indexable");
			return NULL;
	}
}

static inline void list_get()
{
	DEBUGPRINT("GET\n");
	
	struct variable *indexable, *index, *item=0;
	indexable = (struct variable*)ifo_pop(operand_stack);
	index = (struct variable*)ifo_pop(operand_stack);
	
	switch (index->type) {
		case VAR_INT:
			item = list_get_int(indexable, index);
			break;
		case VAR_STR:
			if (indexable->map)
				item = (struct variable*)map_get(indexable->map, index->str);
			if (!item)
				item = default_member(indexable, index);
			assert_message(item, "did not find member");
			break;
		default:
			exit_message("bad index type");
			break;
	}
	//DEBUGPRINT("list_get:%s\n", variable_value(item));
	lifo_push(operand_stack, item);
}

int32_t jump(struct byte_array *program)
{
	uint8_t *start = program->current;
	int32_t offset = serial_decode_int(program);
	DEBUGPRINT("JMP %d\n", offset);
	return offset - (program->current - start);
}

static inline int32_t iff(struct byte_array *program)
{
	int32_t offset = serial_decode_int(program);
	struct variable* v = (struct variable*)ifo_pop(operand_stack);
	assert_message(v->type == VAR_INT, "iff needs var_int");
	DEBUGPRINT("IF %d\n", offset);
	return v->integer ? 0 : (VOID_INT)offset;
}

static inline void push_int(struct byte_array *program)
{
	int32_t num = serial_decode_int(program);
	struct variable* var = variable_new_int(num);
	lifo_push(operand_stack, var);
	DEBUGPRINT("INT %d\n", num);
}

static inline void push_float(struct byte_array *program)
{
	float num = serial_decode_float(program);
	struct variable* var = variable_new_float(num);
	lifo_push(operand_stack, var);
	DEBUGPRINT("FLT %f\n", num);
}

struct variable *find_var_in_stack(const struct program_state *state, const struct byte_array *name)
{
	struct map *var_map = state->named_variables;
	return (struct variable*)(struct variable*)map_get(var_map, name);
}

struct variable *find_var(const struct program_state *state, const struct byte_array *name)
{
	struct variable *v = find_var_in_stack(state, name);
	if (!v)
		v = find_var_in_stack(base_state, name);
	return v;
}

static inline void push_var(struct program_state *state)
{	
	struct byte_array *program = state->code;
	struct byte_array* name = serial_decode_string(program);
	DEBUGPRINT("VAR %s\n", byte_array_to_string(name));
	struct variable *v = find_var(state, name);
	assert_message(v, "variable not found");
	lifo_push(operand_stack, (void*)v);
}

static inline void push_str(struct byte_array *program)
{
	struct byte_array* str = serial_decode_string(program);
	struct variable* v = variable_new_str(str);
	lifo_push(operand_stack, (void*)v);
	DEBUGPRINT("STR %s\n", byte_array_to_string(str));
}

static inline void push_fnc(struct byte_array *program)
{
	DEBUGPRINT("FNC\n");
	uint32_t fcodelen = serial_decode_int(program);
	struct byte_array* fbody = byte_array_new_size(fcodelen);
	memcpy(fbody->data, program->current, fcodelen);
	struct variable* var = variable_new_fnc((struct byte_array*)fbody);
	lifo_push(operand_stack, (void*)var);
	program->current += fcodelen;
}

static inline void set(struct program_state *state)
{	
	struct variable* from_stack = (struct variable*)ifo_pop(operand_stack);
	struct byte_array *program = state->code;
	const struct byte_array* name = serial_decode_string(program);

	struct variable *from_name = find_var(state, name);
	if (!from_name) { // new variable
		struct map *var_map = state->named_variables;
		from_name = variable_copy(from_stack);
		map_insert(var_map, name, (void*)from_name);
	}
	else
		variable_set(from_name, from_stack);

	DEBUGPRINT("SET %s to %s\n",
			   byte_array_to_string(name),
			   variable_value(from_name));
}

static inline void list_put()
{
	DEBUGPRINT("PUT\n");
	struct variable* recipient = (struct variable*)ifo_pop(operand_stack);
	struct variable* key = (struct variable*)ifo_pop(operand_stack);
	struct variable* value = (struct variable*)(struct variable*)ifo_pop(operand_stack);
	
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
					exit_message("indexing non-indexable");
			} break;
		case VAR_STR:
			if (!recipient->map)
				recipient->map = map_new();
			map_insert(recipient->map, key->str, value);
			break;
		default:
			exit_message("bad index type");
			break;
	}
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
		case VM_EQ:		i = m == n;	break;
		case VM_NEQ:	i = m != n;	break;
		case VM_OR:		i = m || n;	break;
		case VM_GT:		i = n > m;	break;
		case VM_LT:		i = n < m;	break;
		default:
			exit_message("bad math int operator");
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
			exit_message("bad math float operator");
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
		case VM_EQ:		w = variable_new_int(byte_array_equals(ustr, vstr));			break;
		default:		exit_message("unknown string operation");						break;
	}
	return w;
}

static inline void binary_op(enum Opcode op)
{
	const struct variable *u = (const struct variable*)ifo_pop(operand_stack);
	const struct variable *v = (const struct variable*)ifo_pop(operand_stack);
	struct variable *w;
	enum VarType ut = (enum VarType)u->type;
	enum VarType vt = (enum VarType)v->type;
	
	if (vt == VAR_STR)
		w = binary_op_str(op, u, v);
	else if ((ut == VAR_FLT && is_num(vt)) || (vt == VAR_FLT && is_num(ut)))
		w = binary_op_float(op, u, v);
	else if (ut == VAR_INT && vt == VAR_INT) 
		w = binary_op_int(op, u, v);
	else
		exit_message("unknown binary op");
	lifo_push(operand_stack, w);
	
	DEBUGPRINT("%s(%s,%s) = %s\n",
			   num_to_string(opcodes, ARRAY_LEN(opcodes), op),
			   variable_value(v),
			   variable_value(u),
			   variable_value(w));	
}

static inline void unary_op(enum Opcode op)
{
	struct variable *v = (struct variable*)(struct variable*)ifo_pop(operand_stack);
	int32_t m = 0;
	
	assert_message(v->type==VAR_INT, "attempting math on non-numbers");
	int32_t n = v->integer;
	switch (op) {
		case VM_NEG:	m = -n;								 break;
		case VM_NOT:	m = !n;								 break;
		default:		exit_message("bad math operator");	  break;
	}
	
	struct variable *w = variable_new_int(m);
	lifo_push(operand_stack, w);
	
	DEBUGPRINT("%s(%s) = %s\n",
			   num_to_string(opcodes, ARRAY_LEN(opcodes), op),
			   variable_value(v),
			   variable_value(w));	
}

// execute

struct variable* run(struct byte_array *program, bool in_context)
{
	struct program_state *state = in_context ?
	(struct program_state*)ifo_peek(program_stack, 0) :
	program_state_from_code(program);
	
	int32_t jmp = 0;
	byte_array_reset(program);
	INDENT;
	DEBUGPRINT("%srun\n", indentation());
	INDENT;
	while (program->current < program->data + program->size) {
		enum Opcode inst = (enum Opcode)*program->current;
#ifdef DEBUG
		display_program_counter(program);
#endif
		program->current++;
		num_inst_executed++;
		switch (inst) {
			case VM_MUL:
			case VM_DIV:
			case VM_ADD:
			case VM_SUB:
			case VM_AND:
			case VM_EQ:
			case VM_NEQ:
			case VM_GT:
			case VM_LT:
			case VM_OR:		binary_op(inst);			break;
			case VM_NEG:
			case VM_NOT:	unary_op(inst);				break;
			case VM_SET:	set(state);					break;
			case VM_JMP:	jmp = jump(program);		break;
			case VM_IF:		jmp = iff(program);			break;
			case VM_CAL:	func_call();				break;
			case VM_LST:	push_list(program);			break;
			case VM_MAP:	push_map(program);			break;
			case VM_GET:	list_get();					break;
			case VM_PUT:	list_put();					break;
			case VM_INT:	push_int(program);			break;
			case VM_FLT:	push_float(program);		break;
			case VM_STR:	push_str(program);			break;
			case VM_VAR:	push_var(state);			break;
			case VM_FNC:	push_fnc(program);			break;
			default:		exit_message(ERROR_OPCODE);	break;
		}
		program->current += jmp;
		jmp = 0;
	}
	UNDENT;
	DEBUGPRINT("%sdone\n", indentation());
	UNDENT;
	if (!in_context)
		ifo_pop(program_stack);
	return (struct variable*)ifo_peek(operand_stack, 0);
}

struct variable *execute(struct byte_array *program, bridge *callback_to_c)
{
	DEBUGPRINT("execute:\n");
	callback2c = callback_to_c;
	assert_message(program!=0 && program->data!=0, ERROR_NULL);
	byte_array_reset(program);
	struct byte_array* code = serial_decode_string(program);
	
	operand_stack = ifo_new();
	
	num_vars = 0;
#ifdef DEBUG
	indent = 0;
#endif
	program_stack = ifo_new();
	
	clock_t start, end;
	double elapsed;
	start = clock();

	struct variable *v;
	if (!setjmp(trying))
		v = run(code, false);
	else
		v = error;		

	end = clock();
	elapsed = ((double) (end - start)) * 1000 / CLOCKS_PER_SEC;
	DEBUGPRINT("%u instructions took %fms: %f instructions per ms\n", num_inst_executed, elapsed, num_inst_executed/elapsed);

	return v;
}
