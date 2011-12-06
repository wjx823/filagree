/* struct.c
 *
 * implements array, byte_array, [f|l]ifo and map
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <assert.h>

#include "vm.h"
#include "struct.h"
#include "util.h"

#define BYTE_ARRAY_MAX_LEN        1000
#define ERROR_BYTE_ARRAY_LEN    "byte array too long"


// array ///////////////////////////////////////////////////////////////////

struct array *array_new() {
    return array_new_size(0);
}

struct array *array_new_size(uint32_t size) {
    struct array *a = (struct array*)malloc(sizeof(struct array));
    a->data = a->current = NULL;
    a->length = 0;
    return a;
}

void array_del(struct array *a) {
    for (int i=0; i<a->length; i++)
        free(array_get(a, i));
    free(a->data);
    free(a);
}

void array_resize(struct array *a, uint32_t length) {
    a->data = (void**)realloc(a->data, length * sizeof(void*));
    null_check(a->data);
    memset(&a->data[a->length], 0, length-a->length);
    a->length = length;
}

uint32_t array_add(struct array *a, void *datum) {
    a->data = (void**)realloc(a->data, (a->length+1) * sizeof(void*));
    a->data[a->length++] = datum;
    return a->length-1;
}

void array_insert(struct array *a, uint32_t index, void *datum)
{
    a->data = (void**)realloc(a->data, (a->length+1) * sizeof(void*));
	uint32_t i;
	for (i=a->length; i>index; i--)
		a->data[i] = a->data[i-1];
	a->data[i] = datum;
	a->length++;
}

void* array_get(const struct array *a, uint32_t index) {
    assert_message(a && index < a->length, ERROR_INDEX);
    //DEBUGPRINT("array_get %d = %x\n", index, a->data[index]);
    return a->data[index];
}

void array_set(struct array *a, uint32_t index, void* datum) {
    null_check(a);
    null_check(datum);
    if (a->length <= index)
        array_resize(a, index+1);
    //DEBUGPRINT("array_set %d %x\n", index, datum);
    a->data[index] = datum;
}

void array_remove(struct array *a, uint32_t index) {
    assert_message(a && index < a->length, ERROR_INDEX);
    size_t len = (a->length - index) * sizeof(void*);
    memcpy(&a->data[index], &a->data[index+1], len);
    a->data = (void**)realloc(a->data, (--a->length) * sizeof(void*));
}

                                             
// byte_array ///////////////////////////////////////////////////////////////

struct byte_array* byte_array_new() {
    struct byte_array* ba = (struct byte_array*)malloc(sizeof(struct byte_array));
    ba->data = ba->current = 0;
    ba->size = 0;
    return ba;
}

void byte_array_del(struct byte_array* ba) {
    if (ba->data)
        free(ba->data);
    free(ba);
}

struct byte_array* byte_array_new_size(uint32_t size) {
    struct byte_array* ba = (struct byte_array*)malloc(sizeof(struct byte_array));
    ba->data = ba->current = (uint8_t*)malloc(size);
    ba->size = size;
    return ba;
}

void byte_array_resize(struct byte_array* ba, uint32_t size) {
    assert_message(ba->current >= ba->data, "byte_array corrupt");
    assert(size);

    uint32_t delta = ba->current - ba->data;
    ba->data = (uint8_t*)realloc(ba->data, size);
    assert(ba->data);
    ba->current = ba->data + delta;
    ba->size = size;
}

bool byte_array_equals(const struct byte_array *a, const struct byte_array* b) {
    if (a==b)
        return true;
    if (!a != !b) // one is null and the other is not
        return false;
    if (a->size != b->size)
        return false;

    int i;
    for (i = 0; i<a->size; i++)
        if (a->data[i] != b->data[i])
            return false;
    return true;
}

struct byte_array* byte_array_copy(const struct byte_array* original) {
    if (!original)
        return NULL;
    struct byte_array* copy = (struct byte_array*)malloc(sizeof(struct byte_array));
    copy->data = (uint8_t*)malloc(original->size);
    memcpy(copy->data, original->data, original->size);
    copy->size = original->size;
    copy->current = copy->data + (original->current - original->data);
    return copy;
}

void byte_array_append(struct byte_array *a, const struct byte_array* b) {
    null_check(a);
    null_check(b);
    uint32_t offset = a->size;
    byte_array_resize(a, a->size + b->size);
    memcpy(&a->data[offset], b->data, b->size);
	a->current = a->data + a->size;
}

struct byte_array* byte_array_from_string(const char* str)
{
    int len = strlen(str);
    struct byte_array* ba = byte_array_new_size(len);
    memcpy(ba->data, str, len);
    return ba;
}

char* byte_array_to_string(const struct byte_array* ba)
{
    int len = ba->size;
    char* s = (char*)malloc(len+1);
    memcpy(s, ba->data, len);
    s[len] = 0;
    return s;
}

void byte_array_reset(struct byte_array* ba) {
    ba->current = ba->data;
}

struct byte_array* byte_array_concatenate(int n, const struct byte_array* ba, ...) {
    
    struct byte_array* result = byte_array_copy(ba);
    
    va_list argp;
    for(va_start(argp, ba); --n;) {
        struct byte_array* parameter = va_arg(argp, struct byte_array* );
        if (!parameter)
            continue;
        else if (!result)
            result = byte_array_copy(parameter);
        else {
            if (result->size + parameter->size > BYTE_ARRAY_MAX_LEN)
                exit_message(ERROR_BYTE_ARRAY_LEN);
            byte_array_append(result, parameter);
        }
    }

    va_end(argp);

    if (result)
        result->current = result->data + result->size;
    return result;
}

struct byte_array* byte_array_add_byte(struct byte_array *a, uint8_t b) {
    byte_array_resize(a, a->size+1);
	a->current = a->data + a->size;
    a->data[a->size-1] = b;
    return a;
}

void byte_array_print(const char* text, const struct byte_array* ba) {
    int offset = ba->current-ba->data;
    if (text)
        printf("%s @%d\n", text, offset);
    for (int i=0; i<ba->size; i++)
        printf("\t%s%d\n", i==offset?">":" ", ba->data[i]);
}

// lifo ////////////////////////////////////////////////////////////////////

struct lifo* lifo_new() {
    return (struct lifo*)calloc(sizeof(struct lifo), 1);
}

struct lifo_node* lifo_node_new() {
    return (struct lifo_node*)calloc(sizeof(struct lifo_node), 1);
}

void lifo_push(struct lifo* lifo_, void* data)
{
    null_check(data);
    if (!lifo_->head)
        lifo_->head = lifo_->tail = lifo_node_new();
    else {
        struct lifo_node* old_head = lifo_->head;
        lifo_->head = lifo_node_new();
        null_check(lifo_->head);
        lifo_->head->next = old_head;
    }
    lifo_->head->data = data;
}

void* lifo_pop(struct lifo* lifo_)
{
    if (!lifo_->head)
        return NULL;
    void* data = lifo_->head->data;
    lifo_->head = lifo_->head->next;
    null_check(data);
    return data;
}

void* lifo_peek(const struct lifo* lifo, uint8_t index)
{
    null_check(lifo);
    struct lifo_node*p = lifo->head;
    for (; index; index--, p=p->next);
    return p ? p->data : NULL;
}

bool lifo_empty(const struct lifo* lifo)
{
	return lifo->head == NULL && lifo->tail == NULL;
}

// map /////////////////////////////////////////////////////////////////////

static size_t def_hash_func(const struct byte_array* key)
{
    size_t hash = 0;
    int i = 0;
    for (i = 0; i<key->size; i++)
        hash += key->data[i];
    return hash;
}

struct map* map_new()
{
    struct map* m;
    if (!(m =(struct map*)malloc(sizeof(struct map)))) return NULL;
    m->size = 16;
    m->hash_func = def_hash_func;
    
    if (!(m->nodes = (struct hash_node**)calloc(m->size, sizeof(struct hash_node*)))) {
        free(m);
        return NULL;
    }

    return m;
}

void map_del(struct map* m)
{
    DEBUGPRINT("map_destroy\n");
    size_t n;
    struct hash_node *node, *oldnode;
    
    for(n = 0; n<m->size; ++n) {
        node = m->nodes[n];
        while (node) {
            byte_array_del(node->key);
            oldnode = node;
            node = node->next;
            free(oldnode);
        }
    }
    free(m->nodes);
    free(m);
}

int map_insert(struct map* m, const struct byte_array* key, void *data)
{
    struct hash_node *node;
    size_t hash = m->hash_func(key) % m->size;    
    
    node = m->nodes[hash];
    while (node) {
        if (byte_array_equals(node->key, key)) {
            node->data = data;
            return 0;
        }
        node = node->next;
    }

    if (!(node = (struct hash_node*)malloc(sizeof(struct hash_node))))
        return -1;
    if (!(node->key = byte_array_copy(key))) {
        free(node);
        return -1;
    }
    node->data = data;
    node->next = m->nodes[hash];
    m->nodes[hash] = node;

    return 0;
}

struct array* map_keys(const struct map* m) {
    struct array *a = array_new();
    for (int i=0; i<m->size; i++)
        for (const struct hash_node* n = m->nodes[i]; n; n=n->next)
            if (n->data)
                array_add(a, n->key);
    return a;
}

struct array* map_values(const struct map* m) {
    struct array *a = array_new();
    for (int i=0; i<m->size; i++)
        for (const struct hash_node* n = m->nodes[i]; n; n=n->next)
            if (n->data)
                array_add(a, n->data);
    return a;
}

int map_remove(struct map* m, const struct byte_array* key)
{
    struct hash_node *node, *prevnode = NULL;
    size_t hash = m->hash_func(key)%m->size;
    
    node = m->nodes[hash];
    while(node) {
        if (byte_array_equals(node->key, key)) {
            byte_array_del(node->key);
            if (prevnode) prevnode->next = node->next;
            else m->nodes[hash] = node->next;
            free(node);
            return 0;
        }
        prevnode = node;
        node = node->next;
    }    
    return -1;
}

bool map_has(const struct map* m, const struct byte_array* key) {
    struct hash_node *node;
    size_t hash = m->hash_func(key) % m->size;
    node = m->nodes[hash];
    while (node) {
        if (byte_array_equals(node->key, key))
            return true;
        node = node->next;
    }
    return false;
}

void *map_get(const struct map* m, const struct byte_array* key)
{
    struct hash_node *node;
    size_t hash = m->hash_func(key) % m->size;
    node = m->nodes[hash];
    while (node) {
        if (byte_array_equals(node->key, key))
            return node->data;
        node = node->next;
    }
    return NULL;
}

int map_resize(struct map* m, size_t size)
{
    DEBUGPRINT("map_resize\n");
    struct map newtbl;
    size_t n;
    struct hash_node *node,*next;
    
    newtbl.size = size;
    newtbl.hash_func = m->hash_func;
    
    if (!(newtbl.nodes = (struct hash_node**)calloc(size, sizeof(struct hash_node*))))
        return -1;
    
    for(n = 0; n<m->size; ++n) {
        for(node = m->nodes[n]; node; node = next) {
            next = node->next;
            map_insert(&newtbl, node->key, node->data);
            map_remove(m, node->key);
            
        }
    }
    
    free(m->nodes);
    m->size = newtbl.size;
    m->nodes = newtbl.nodes;
    
    return 0;
}

// in case of intersection, a wins
void map_update(struct map *a, const struct map *b)
{
    const struct array *keys = map_keys(b);
    for (int i=0; i<keys->length; i++) {
        const struct byte_array *key = (const struct byte_array*)array_get(keys, i);
		if (!map_has(a, key))
			map_insert(a, key, map_get(b, key));
    }
}
