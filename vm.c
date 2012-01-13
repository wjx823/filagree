#include <stdio.h>
#include <signal.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>
#include "util.h"
#include "serial.h"
#include "vm.h"
#include "sys.h"

void garbage_collect();
struct stack *program_stack;
struct stack *operand_stack;
struct stack *rhs;
uint32_t num_inst_executed;

struct variable *run(struct byte_array *program, bool in_context);
struct variable *rhs_pop();
static void dst();
void src_size();
void display_code(struct byte_array *code);

bool runtime = false;
uint32_t num_vars = 0;
struct variable* error = 0;


#ifdef DEBUG

#define VM_DEBUGPRINT(...) fprintf( stderr, __VA_ARGS__ ); if (!runtime) return;

void display_instruction(struct byte_array *program);
void print_operand_stack();

uint8_t indent;
#define INDENT indent++;
#define UNDENT indent--;

#else // not DEBUG

#define INDENT
#define UNDENT

#define VM_DEBUGPRINT(...)

#endif // not DEBUG


// assertions //////////////////////////////////////////////////////////////

jmp_buf trying;

static void vm_exit() {
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

// state ///////////////////////////////////////////////////////////////////

struct program_state {
	//	struct byte_array *code;
	struct map *named_variables;
	struct array *all_variables;
	uint32_t pc;
};

struct program_state *vm_state = NULL;
#define VM_NAME "vm"

struct program_state *program_state_new()
{
	struct program_state *state = (struct program_state*)malloc(sizeof(struct program_state));
	state->named_variables = map_new();
	state->all_variables = array_new();
	stack_push(program_stack, state);
	return state;
}

void vm_init()
{
	program_stack = stack_new();
	operand_stack = stack_new();

	if (!vm_state) {
		struct variable *vm_var = func_map();
		vm_state = program_state_new();
		map_insert(vm_state->named_variables, byte_array_from_string(VM_NAME), vm_var);
	}

	runtime = true;
	num_vars = 0;
	num_inst_executed = 0;
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

const char *var_type_str(enum VarType vt)
{
	return NUM_TO_STRING(var_types, vt);
}

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

 struct variable* variable_new_nil()
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
	v->list = list ? list : array_new();
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
	struct array* list = v->list;

	enum VarType vt = (enum VarType)v->type;
	switch (vt) {
		case VAR_NIL:	sprintf(str, "nil");									break;
		case VAR_INT:	sprintf(str, "%d", v->integer);							break;
		case VAR_BOOL:	sprintf(str, "%s", v->boolean ? "true" : "false");		break;
		case VAR_FLT:	sprintf(str, "%f", v->floater);							break;
		case VAR_STR:	sprintf(str, "%s", byte_array_to_string(v->str));		break;
		case VAR_FNC:	sprintf(str, "f(%dB)", v->str->size);					break;
		case VAR_C:		sprintf(str, "c-function");								break;
		case VAR_MAP:															break;
/*		case VAR_SRC: {
			strcpy(str, "[");
			vm_null_check(list);
			for (int i=0; i<list->length; i++) {
				struct variable* element = (struct variable*)array_get(list, i);
				vm_null_check(element);
				const char *c = i ? "," : "";
				sprintf(str, "%s%s%s", str, c, variable_value(element));
			}
			strcat(str, "]");
		} break;*/
		case VAR_LST: {
			strcpy(str, "[");
			vm_null_check(list);
			for (int i=0; i<list->length; i++) {
				struct variable* element = (struct variable*)array_get(list, i);
				vm_null_check(element);
				const char *q = (element->type == VAR_STR || element->type == VAR_FNC) ? "'" : "";
				const char *c = i ? "," : "";
				sprintf(str, "%s%s%s%s%s", str, c, q, variable_value(element), q);
			}
		} break;
		default:
			vm_exit_message(ERROR_VAR_TYPE);
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
			strcat(str, variable_value((const struct variable*)array_get(b,i)));
		}
		strcat(str, "]");
	}
	else if (vt == VAR_LST)
		strcat(str, "]");

	return str;
}

struct variable *variable_pop()
{
	struct variable *v = stack_pop(operand_stack);
	//DEBUGPRINT("\nvariable_pop\n");// %s\n", variable_value(v));
	//	print_operand_stack();
	return v;
}

void variable_push(struct variable *v)
{
	stack_push(operand_stack, v);
	//DEBUGPRINT("\nvariable_push\n");
	//print_operand_stack();
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
	struct program_state *state = (struct program_state*)stack_peek(program_stack, 0);
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
	struct program_state *state = (struct program_state*)stack_peek(program_stack, 0);
	struct array *vars = state->all_variables; 
	for (int i=0; i<vars->length; i++) {
		struct variable *v = (struct variable*)array_get(vars, i);
		mark(v);
		sweep(v);
	}
}

// display /////////////////////////////////////////////////////////////////

#ifdef DEBUG

const struct number_string opcodes[] = {
	{VM_NIL,		"NIL"},
	{VM_INT,		"INT"},
	{VM_BOOL,		"BUL"},
	{VM_FLT,		"FLT"},
	{VM_STR,		"STR"},
	{VM_VAR,		"VAR"},
	{VM_FNC,		"FNC"},
	{VM_SRC,		"SRC"},
	{VM_LST,		"LST"},
	{VM_DST,		"DST"},
	{VM_MAP,		"MAP"},
	{VM_GET,		"GET"},
	{VM_PUT,		"PUT"},
	{VM_ADD,		"ADD"},
	{VM_SUB,		"SUB"},
	{VM_MUL,		"MUL"},
	{VM_DIV,		"DIV"},
	{VM_AND,		"AND"},
	{VM_OR,			"ORR"},
	{VM_NOT,		"NOT"},
	{VM_NEG,		"NEG"},
	{VM_EQU,		"EQU"},
	{VM_NEQ,		"NEQ"},
	{VM_GT,			"GTN"},
	{VM_LT,			"LTN"},
	{VM_IF,			"IFF"},
	{VM_JMP,		"JMP"},
	{VM_CAL,		"CAL"},
	{VM_MET,		"MET"},
	{VM_RET,		"RET"},
	{VM_ITR,		"ITR"},
	{VM_COM,		"COM"},
};

void print_operand_stack()
{
	struct variable *operand;
	for (int i=0; (operand = stack_peek(operand_stack, i)); i++)
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

static void display_program_counter(const struct byte_array *program)
{
	DEBUGPRINT("%s%2ld:%3d ", indentation(), program->current-program->data, *program->current);
}

void display_code(struct byte_array *code)
{
	bool was_running = runtime;
	runtime = false;

	INDENT
	run(code, false);
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
	UNDENT

	DEBUGPRINT("%s%s instructions:\n", indentation(), title);
	byte_array_reset(program);
	struct byte_array* code = serial_decode_string(program);

	display_code(code);

	UNDENT
	UNDENT
}

#else // not DEBUG

void display_code(struct byte_array *code) {}
const struct number_string opcodes[] = {};

#endif // DEBUG

// instruction implementations /////////////////////////////////////////////

void src_size(int32_t size)
{
	if (!rhs)
		rhs = stack_new();
	if (size > 1)
		while (stack_peek(rhs,1))
			stack_pop(rhs);
	else if (!stack_empty(rhs))
		return;

	//    struct stack *rhs = stack_new();
	while (size--)
        stack_push(rhs, variable_pop());
    //stack_push(rhs_stack, rhs);
}

static void src(enum Opcode op, struct byte_array *program)
{
	int32_t size = serial_decode_int(program);
	VM_DEBUGPRINT("%s %d\n", NUM_TO_STRING(opcodes, op), size);
	src_size(size);
}

void vm_call()
{
	// get the function pointer from the stack
	struct variable *func = runtime ? variable_pop() : NULL;
	INDENT

	// call the function
	switch (func->type) {
		case VAR_FNC:
			run(func->str, false);
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

static void func_call()
{
	VM_DEBUGPRINT("VM_CAL\n");
	vm_call();
}

static void push_list(struct byte_array *program)
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

static void push_map(struct byte_array *program)
{
	int32_t num_items = serial_decode_int(program);
	DEBUGPRINT("MAP %d", num_items);
	if (!runtime)
		VM_DEBUGPRINT("\n");
	struct map *map = map_new();
	while (num_items--) {
		struct variable* value = variable_pop();
		struct variable* key = variable_pop();
		assert_message(key->type==VAR_STR, "non-string map index");
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


// run /////////////////////////////////////////////////////////////////////

static struct variable *list_get_int(const struct variable *indexable,
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

void lookup(struct variable *indexable, struct variable *index)
{
	if (!runtime)
		VM_DEBUGPRINT("GET\n");

	struct variable *item=0;

	switch (index->type) {
		case VAR_INT:
			item = list_get_int(indexable, index);
			break;
		case VAR_STR:
			if (indexable->map)
				item = (struct variable*)map_get(indexable->map, index->str);
			if (!item)
				item = builtin_method(indexable, index);
			assert_message(item, "did not find member");
			break;
		default:
			vm_exit_message("bad index type");
			break;
	}
	DEBUGPRINT("%s\n", variable_value(item));
	variable_push(item);
}

static void list_get()
{
	DEBUGPRINT("GET ");
	if (!runtime)
		VM_DEBUGPRINT("\n");
	struct variable *indexable, *index;
	indexable = variable_pop();
	index = variable_pop();
	lookup(indexable, index);
}

static void method(struct byte_array *program)
{
	DEBUGPRINT("MET ");
	if (!runtime)
		VM_DEBUGPRINT("\n");
	struct variable *indexable, *index;
	indexable = variable_pop();
	index = variable_pop();
	lookup(indexable, index);
	//	struct stack *rhs = stack_peek(rhs_stack, 0);
	stack_push(rhs, indexable);
	vm_call();
}


static int32_t jump(struct byte_array *program)
{
	uint8_t *start = program->current;
	int32_t offset = serial_decode_int(program);
	DEBUGPRINT("JMP %d\n", offset);
	if (!runtime)
		return 0;

	return offset - (program->current - start);
}

bool test_operand()
{
	struct variable* v = variable_pop();
	bool indeed = false;
	switch (v->type) {
		case VAR_NIL:	indeed = false;						break;
		case VAR_BOOL:	indeed = v->boolean;				break;
		case VAR_INT:	indeed = v->integer;				break;
		default:		vm_exit_message("bad iff operand");	break;
	}
	return indeed;
}

static int32_t iff(struct byte_array *program)
{
	int32_t offset = serial_decode_int(program);
	DEBUGPRINT("IF %d\n", offset);
	if (!runtime)
		return 0;
	return test_operand() ? 0 : (VOID_INT)offset;
}

static void push_nil()
{
	struct variable* var = variable_new_nil();
	VM_DEBUGPRINT("NIL\n");
	variable_push(var);
}

static void push_int(struct byte_array *program)
{
	int32_t num = serial_decode_int(program);
	VM_DEBUGPRINT("INT %d\n", num);
	struct variable* var = variable_new_int(num);
	variable_push(var);
}

static void push_bool(struct byte_array *program)
{
	int32_t num = serial_decode_int(program);
	VM_DEBUGPRINT("BOOL %d\n", num);
	struct variable* var = variable_new_bool(num);
	variable_push(var);
}

static void push_float(struct byte_array *program)
{
	float num = serial_decode_float(program);
	VM_DEBUGPRINT("FLT %f\n", num);
	struct variable* var = variable_new_float(num);
	variable_push(var);
}

struct variable *find_var(const struct byte_array *name)
{
	const struct program_state *state = stack_peek(program_stack, 0);
	struct map *var_map = state->named_variables;
	struct variable *v = map_get(var_map, name);
	//DEBUGPRINT("find_var(%s) in %p,%p = %p\n", byte_array_to_string(name), state, var_map, v);
	if (!v)
		v = map_get(vm_state->named_variables, name);
	return v;
}

static void push_var(struct byte_array *program)
{
	struct byte_array* name = serial_decode_string(program);
	VM_DEBUGPRINT("VAR %s\n", byte_array_to_string(name));
	struct variable *v = find_var(name);
	vm_assert(v, "variable %s not found", byte_array_to_string(name));
	variable_push(v);
}

static void push_str(struct byte_array *program)
{
	struct byte_array* str = serial_decode_string(program);
	VM_DEBUGPRINT("STR '%s'\n", byte_array_to_string(str));
	struct variable* v = variable_new_str(str);
	variable_push(v);
}

static void push_fnc(struct byte_array *program)
{
	uint32_t fcodelen = serial_decode_int(program);
	struct byte_array* fbody = byte_array_new_size(fcodelen);
	memcpy(fbody->data, program->current, fcodelen);

	DEBUGPRINT("FNC %u\n", fcodelen);
	display_code(fbody);

	if (runtime) {
		struct variable* var = variable_new_fnc((struct byte_array*)fbody);
		variable_push(var);
	}

	program->current += fcodelen;
}

void set_named_variable(struct program_state *state,
						const struct byte_array *name,
						const struct variable *value)
{
	struct map *var_map = state->named_variables;
	struct variable *to_var = find_var(name);

	if (!to_var) { // new variable
		to_var = variable_copy(value);
		to_var->name = byte_array_copy(name);
	} else
		variable_set(to_var, value);

	map_insert(var_map, name, to_var);

	//DEBUGPRINT(" (SET %s to %s in {%p,%p,%p})\n", byte_array_to_string(name), variable_value(to_var), state, var_map, to_var);
}

struct variable *rhs_pop()
{
	//	struct stack *rhs = stack_peek(rhs_stack, 0);
	struct variable *value = stack_pop(rhs);
	if (!value)
		value = variable_new_nil();
	return value;
}

static void set(struct program_state *state, struct byte_array *program)
{
	struct byte_array *name = serial_decode_string(program);	// destination variable name
	if (!runtime)
		VM_DEBUGPRINT("SET %s\n", byte_array_to_string(name));

	struct variable *value = rhs_pop();
	DEBUGPRINT("SET %s to %s\n", byte_array_to_string(name), variable_value((value)));
	set_named_variable(state, name, value);                     // set the variable to the value
}

static void dst()
{
	VM_DEBUGPRINT("DST\n");
	//    stack_pop(rhs_stack);
	while (!stack_empty(rhs))
		stack_pop(rhs);
}

static void list_put()
{
	DEBUGPRINT("PUT");
	if (!runtime)
		VM_DEBUGPRINT("\n");
	struct variable* recipient = variable_pop();
	struct variable* key = variable_pop();
	struct variable *value = rhs_pop();

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

static struct variable *binary_op_int(enum Opcode op,
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
		case VM_GT:		i = m > n;	break;
		case VM_LT:		i = m < n;	break;
		default:
			vm_exit_message("bad math int operator");
			return NULL;
	}
	return variable_new_int(i);
}

static struct variable *binary_op_float(enum Opcode op,
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

static bool is_num(enum VarType vt) {
	return vt == VAR_INT || vt == VAR_FLT;
}

static struct variable *binary_op_str(enum Opcode op,
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

static bool variable_compare(const struct variable *u,
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

static struct variable *binary_op_lst(enum Opcode op,
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

static void binary_op(enum Opcode op)
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
		else if (ut == VAR_INT && vt == VAR_INT)	w = binary_op_int(op, v, u);
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

static void unary_op(enum Opcode op)
{
	if (!runtime)
		VM_DEBUGPRINT("%s\n", NUM_TO_STRING(opcodes, op));

	struct variable *v = (struct variable*)variable_pop();
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

// FOR who IN what WHERE where DO how
static void iterate(enum Opcode op, struct program_state *state, struct byte_array *program)
{
	struct byte_array *who = serial_decode_string(program);
	struct byte_array *where = serial_decode_string(program);
	struct byte_array *how = serial_decode_string(program);

#ifdef DEBUG
	DEBUGPRINT("%s %s\n",
				  NUM_TO_STRING(opcodes, op),
				  byte_array_to_string(who));
	if (!runtime) {
		if (where) {
			DEBUGPRINT("%s\tWHERE\n", indentation());
			display_code(where);
		}
		DEBUGPRINT("%s\tDO\n", indentation());
		display_code(how);
		return;
	}
#endif

	bool comprehending = (op == VM_COM);
	struct variable *result = comprehending ? variable_new_list(NULL) : NULL;

	struct variable *what = variable_pop();
	for (int i=0; i<what->list->length; i++) {

		struct variable *that = array_get(what->list, i);
		set_named_variable(state, who, that);

		byte_array_reset(where);
		byte_array_reset(how);
		run(where, true);
		if (test_operand()) {

			run(how, true);

			if (comprehending) {
				struct variable *item = stack_pop(operand_stack);
				array_add(result->list, item);
			}
		}
	}

	if (comprehending)
		stack_push(operand_stack, result);
}

struct variable *run(struct byte_array *program, bool in_context)
{
	struct program_state *state = NULL;
	if (runtime) {
		if (in_context)
			state = stack_peek(program_stack, 0);
		if (!state)
			state = program_state_new();
	}

	while (program->current < program->data + program->size) {
		enum Opcode inst = (enum Opcode)*program->current;
#ifdef DEBUG
		display_program_counter(program);
#endif
		program->current++; // increment past the instruction
		if (runtime)
			num_inst_executed++;

		if (inst == VM_RET) {
			src(inst, program);
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
			case VM_SRC:	src(inst, program);				break;
			case VM_DST:	dst();							break;
			case VM_SET:	set(state, program);			break;
			case VM_JMP:	pc_offset = jump(program);		break;
			case VM_IF:		pc_offset = iff(program);		break;
			case VM_CAL:	func_call();					break;
			case VM_LST:	push_list(program);				break;
			case VM_MAP:	push_map(program);				break;
			case VM_GET:	list_get();						break;
			case VM_PUT:	list_put();						break;
			case VM_NIL:	push_nil();						break;
			case VM_INT:	push_int(program);				break;
			case VM_FLT:	push_float(program);			break;
			case VM_BOOL:	push_bool(program);				break;
			case VM_STR:	push_str(program);				break;
			case VM_VAR:	push_var(program);				break;
			case VM_FNC:	push_fnc(program);				break;
			case VM_MET:	method(program);				break;
			case VM_COM:
			case VM_ITR:	iterate(inst, state, program);	break;
			default:		vm_exit_message(ERROR_OPCODE);	break;
		}

		program->current += pc_offset;
	}

	if (runtime && !in_context)
		stack_pop(program_stack);
	return (struct variable*)stack_peek(operand_stack, 0);
}

struct variable *execute(struct byte_array *program, bool in_context, bridge *callback_to_c)
{
	DEBUGPRINT("execute:\n");
	callback2c = callback_to_c;
	vm_assert(program!=0 && program->data!=0, ERROR_NULL);
	byte_array_reset(program);
	struct byte_array* code = serial_decode_string(program);

#ifdef DEBUG
	indent = 1;
#endif

	clock_t start, end;
	double elapsed;
	start = clock();

	struct variable *v;
	if (!setjmp(trying))
		v = run(code, in_context);
	else
		v = error;

	end = clock();
	elapsed = ((double) (end - start)) * 1000 / CLOCKS_PER_SEC;
	//	DEBUGPRINT("%u instructions took %fms: %f instructions per ms\n", num_inst_executed, elapsed, num_inst_executed/elapsed);

	return v;
}
