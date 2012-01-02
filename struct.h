/* struct.h
 *
 * APIs for array, byte_array, [f|l]ifo and map
 */

#ifndef STRUCT_H
#define STRUCT_H


#include <inttypes.h>
#include <stdbool.h>
#include <stdlib.h>


#define ERROR_INDEX	"index out of bounds"
#define ERROR_NULL "null pointer"


// array ///////////////////////////////////////////////////////////////////

struct array {
	void **data, **current;
	uint32_t length;
};

struct array *array_new();
void array_del(struct array *a);
struct array *array_new_size(uint32_t size);
void array_resize(struct array *a, uint32_t length);
uint32_t array_add(struct array *a , void *datum);
void array_insert(struct array *a, uint32_t index, void *datam);
void* array_get(const struct array *a, uint32_t index);
void array_set(struct array *a, uint32_t index, void *datum);
void *array_remove(struct array *a, uint32_t index);

// byte_array ///////////////////////////////////////////////////////////////

struct byte_array {
	uint8_t *data, *current;
	int32_t size;
};

struct byte_array* byte_array_new();
struct byte_array* byte_array_new_size(uint32_t size);
void byte_array_append(struct byte_array *a, const struct byte_array* b);
struct byte_array* byte_array_from_string(const char* str);
char* byte_array_to_string(const struct byte_array* ba);
void byte_array_del(struct byte_array* ba);
struct byte_array* byte_array_copy(const struct byte_array* original);
struct byte_array* byte_array_add_byte(struct byte_array *a, uint8_t b);
void byte_array_reset(struct byte_array* ba);
void byte_array_resize(struct byte_array* ba, uint32_t size);
bool byte_array_equals(const struct byte_array *a, const struct byte_array* b);
struct byte_array* byte_array_concatenate(int n, const struct byte_array* ba, ...);
void byte_array_print(const char* text, const struct byte_array* ba);

// stack ////////////////////////////////////////////////////////////////////

struct stack_node {
	void* data;
	struct stack_node* next;
};

struct stack {
	struct stack_node* head;
	struct stack_node* tail;
};

struct stack* stack_new();
struct stack_node* stack_node_new();
void fifo_push(struct stack* fifo, void* data);
void stack_push(struct stack* stack, void* data);
void* stack_pop(struct stack* stack);
void* stack_peek(const struct stack* stack, uint8_t index);
bool stack_empty(const struct stack* stack);

// map /////////////////////////////////////////////////////////////////////

struct hash_node {
	struct byte_array *key;
	void *data;
	struct hash_node *next;
};

struct map {
	size_t size;
	struct hash_node **nodes;
	size_t (*hash_func)(const struct byte_array*);
};

struct map* map_new();
void map_del(struct map* map);
int map_insert(struct map* map, const struct byte_array *key, void *data);
int map_remove(struct map* map, const struct byte_array *key);
void *map_get(const struct map* map, const struct byte_array *key);
bool map_has(const struct map* map, const struct byte_array *key);
int map_resize(struct map* map, size_t size);
struct array* map_keys(const struct map* m);
struct array* map_values(const struct map* m);
void map_update(struct map *a, const struct map *b);

#endif // STRUCT_H
