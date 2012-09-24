/* struct.c
 *
 * implements array, byte_array, lifo and map
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <assert.h>
#include <ctype.h>

#include "vm.h"
#include "struct.h"
#include "util.h"

#define BYTE_ARRAY_MAX_LEN      10000
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

void *list_remove(void *data, uint32_t *end, uint32_t start, int32_t length, size_t width)
{
    null_check(data);
    null_check(end);
    length = length < 0 ? *end - start : length;
    assert_message(!length || (start < *end && start+length <= *end), "index out of bounds");
    
    memmove((uint8_t*)data+start*width, (uint8_t*)data+(start+length)*width, (*end-start-length)*width);
    *end -= (uint32_t)length;
    return realloc(data, *end * width);
}

void array_remove(struct array *self, uint32_t start, int32_t length) {
    self->data = (void**)list_remove(self->data, &self->length, start, length, sizeof(void*));
}

struct array *array_copy(const struct array* original) {
    if (!original)
        return NULL;
    struct array* copy = (struct array*)malloc(sizeof(struct array));
    copy->data = (void**)malloc(original->length * sizeof(void**));
    memcpy(copy->data, original->data, original->length * sizeof(void*));
    copy->length = original->length;
    copy->current = copy->data + (original->current - original->data);
    return copy;
}

struct array *array_part(struct array *within, uint32_t start, uint32_t length)
{
    struct array *p = array_copy(within);
    array_remove(p, start+length, within->length-start-length);
    array_remove(p, 0, start);
    return p;
}

void array_append(struct array *a, const struct array* b)
{
    null_check(a);
    null_check(b);
    uint32_t alen = a->length;
    array_resize(a, alen + b->length);
    memcpy(&a->data[alen], b->data, b->length * sizeof(void*));
    a->current = a->data + alen;
}

// byte_array ///////////////////////////////////////////////////////////////

struct byte_array *byte_array_new() {
    struct byte_array* ba = (struct byte_array*)malloc(sizeof(struct byte_array));
    ba->data = ba->current = 0;
    ba->length = 0;
    return ba;
}

void byte_array_del(struct byte_array* ba) {
    if (ba->data)
        free(ba->data);
    free(ba);
}

struct byte_array *byte_array_new_size(uint32_t size) {
    struct byte_array* ba = (struct byte_array*)malloc(sizeof(struct byte_array));
    ba->data = ba->current = (uint8_t*)malloc(size);
    ba->length = size;
    return ba;
}

void byte_array_resize(struct byte_array* ba, uint32_t size) {
    assert_message(ba->current >= ba->data, "byte_array corrupt");
    uint32_t delta = ba->current - ba->data;
    ba->data = (uint8_t*)realloc(ba->data, size);
    assert(ba->data);
    ba->current = ba->data + delta;
    ba->length = size;
}

bool byte_array_equals(const struct byte_array *a, const struct byte_array* b) {
    if (a==b)
        return true;
    if (!a != !b) // one is null and the other is not
        return false;
    if (a->length != b->length)
        return false;
    
    int i;
    for (i = 0; i<a->length; i++)
        if (a->data[i] != b->data[i])
            return false;
    return true;
}

struct byte_array *byte_array_copy(const struct byte_array* original) {
    if (!original)
        return NULL;
    struct byte_array* copy = (struct byte_array*)malloc(sizeof(struct byte_array));
    copy->data = (uint8_t*)malloc(original->length);
    memcpy(copy->data, original->data, original->length);
    copy->length = original->length;
    copy->current = copy->data + (original->current - original->data);
    return copy;
}

void byte_array_append(struct byte_array *a, const struct byte_array* b) {
    null_check(a);
    null_check(b);
    uint32_t offset = a->length;
    byte_array_resize(a, a->length + b->length);
    memcpy(&a->data[offset], b->data, b->length);
    a->current = a->data + a->length;
}

void byte_array_remove(struct byte_array *self, uint32_t start, int32_t length) {
    self->data = (uint8_t*)list_remove(self->data, &self->length, start, length, sizeof(uint8_t));
}

struct byte_array *byte_array_part(struct byte_array *within, uint32_t start, uint32_t length)
{
    struct byte_array *p = byte_array_copy(within);
    byte_array_remove(p, start+length, within->length-start-length);
    byte_array_remove(p, 0, start);
    return p;
}

struct byte_array *byte_array_from_string(const char* str)
{
    int len = strlen(str);
    struct byte_array* ba = byte_array_new_size(len);
    memcpy(ba->data, str, len);
    return ba;
}

char* byte_array_to_string(const struct byte_array* ba)
{
    int len = ba->length;
    char* s = (char*)malloc(len+1);
    memcpy(s, ba->data, len);
    s[len] = 0;
    return s;
}

void byte_array_reset(struct byte_array* ba) {
    ba->current = ba->data;
}

struct byte_array *byte_array_concatenate(int n, const struct byte_array* ba, ...)
{
    struct byte_array* result = byte_array_copy(ba);

    va_list argp;
    for(va_start(argp, ba); --n;) {
        struct byte_array* parameter = va_arg(argp, struct byte_array* );
        if (!parameter)
            continue;
        assert_message(result->length + parameter->length < BYTE_ARRAY_MAX_LEN, ERROR_BYTE_ARRAY_LEN);
        byte_array_append(result, parameter);
    }

    va_end(argp);

    if (result)
        result->current = result->data + result->length;
    return result;
}

struct byte_array *byte_array_add_byte(struct byte_array *a, uint8_t b) {
    byte_array_resize(a, a->length+1);
    a->current = a->data + a->length;
    a->data[a->length-1] = b;
    return a;
}

void byte_array_print(const char* text, const struct byte_array* ba) {
    int offset = ba->current-ba->data;
    if (text)
        printf("%s @%d\n", text, offset);
    for (int i=0; i<ba->length; i++)
        printf("\t%s%d\n", i==offset?">":" ", ba->data[i]);
}

int32_t byte_array_find(struct byte_array *within, struct byte_array *sought, uint32_t start)
{
    null_check(within);
    null_check(sought);
    
    uint32_t ws = within->length;
    uint32_t ss = sought->length;
    if (start >= within->length)
        return -1;
    
    uint8_t *wd = within->data;
    uint8_t *sd = sought->data;
    for (int32_t i=start; i<ws-ss+1; i++)
        if (!memcmp(wd + i, sd, ss)) 
            return i; 
    
    return -1;
}

struct byte_array *byte_array_replace(struct byte_array *within, struct byte_array *replacement, uint32_t start, int32_t length)
{
    null_check(within);
    null_check(replacement);
    uint32_t ws = within->length;
    assert_message(start < ws, "index out of bounds");
    if (length < 0)
        length = ws - start;
    
    int32_t new_length = within->length - length + replacement->length;
    struct byte_array *replaced = byte_array_new_size(new_length);
    
    memcpy(replaced->data, within->data, start);
    memcpy(replaced->data + start, replacement->data, replacement->length);
    memcpy(replaced->data + start + replacement->length, within->data + start + length, within->length - start - length);
    
    return replaced;
}


// stack ////////////////////////////////////////////////////////////////////

struct stack* stack_new() {
    return (struct stack*)calloc(sizeof(struct stack), 1);
}

struct stack_node* stack_node_new() {
    return (struct stack_node*)calloc(sizeof(struct stack_node), 1);
}

void stack_push(struct stack* stack, void* data)
{
    null_check(data);
    //DEBUGPRINT("stack_push %x on %x\n", data, stack);
    if (!stack->head)
        stack->head = stack->tail = stack_node_new();
    else {
        struct stack_node* old_head = stack->head;
        stack->head = stack_node_new();
        null_check(stack->head);
        stack->head->next = old_head;
    }
    stack->head->data = data;
}

void* stack_pop(struct stack* stack)
{
    if (!stack->head)
        return NULL;
    void* data = stack->head->data;
    stack->head = stack->head->next;
    null_check(data);
    //DEBUGPRINT("stack_pop %x de %x\n", data, stack);
    return data;
}

void* stack_peek(const struct stack* stack, uint8_t index)
{
    null_check(stack);
    struct stack_node*p = stack->head;
    for (; index && p; index--, p=p->next);
    return p ? p->data : NULL;
}

bool stack_empty(const struct stack* stack)
{
    null_check(stack);
    return stack->head == NULL;
}

// map /////////////////////////////////////////////////////////////////////

static size_t def_hash_func(const struct byte_array* key)
{
    size_t hash = 0;
    int i = 0;
    for (i = 0; i<key->length; i++)
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

struct map *map_copy(struct map *original)
{
    struct map *copy = map_new();
    if (!original)
        return copy;
    map_update(copy, original);
    return copy;
}