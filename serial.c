/*
 *    serial.c
 *
 *    stream serialization - serialies and deserializes byte_array and calls-back on each element
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "serial.h"
#include "struct.h"
#include "vm.h"
#include "util.h"

// functions ///////////////////////////////////////////////////////////////

// tells how many bytes are needed to encode this value
uint8_t encode_int_size(int32_t value)
{
    uint8_t i;
    value = (value >= 0 ? value : -value) >> 6;
    for (i=1; value; i++, value >>=7);
    return i;
}

struct byte_array *encode_int(struct byte_array *buf, int32_t value)
{
    if (!buf)
		buf = byte_array_new();
    uint8_t growth = encode_int_size(value);
    byte_array_resize(buf, buf->length + growth);
    bool neg = value < 0;
    value = abs(value);
    uint8_t byte = (value & 0x3F) | ((value >= 0x40) ? 0x80 : 0) | (neg ? 0x40 : 0);
    *buf->current++ = byte;
    value >>= 6;
    while (value) {
        byte = (value & 0x7F) | ((value >= 0x80) ? 0x80 : 0); 
        *buf->current++ = byte; 
        value >>= 7;
    }
    return buf;
}

struct byte_array* serial_encode_int(struct byte_array* buf, int32_t value) {
    if (!buf)
        buf = byte_array_new();
    encode_int(buf, value);
    return buf;
}

int32_t serial_decode_int(struct byte_array* buf)
{
    bool neg = *buf->current & 0x40;
    int32_t ret = *buf->current & 0x3F;
    int bitpos = 6;
    while ((*buf->current++ & 0x80) && (bitpos < (sizeof(int32_t)*8))) {
        ret |= (*buf->current & 0x7F) << bitpos;
        bitpos += 7;
    }
    return neg ? -ret : ret;
}

float serial_decode_float(struct byte_array* buf)
{
    float f;
    uint8_t *uf = (uint8_t*)&f;
    for (int i=4; i; i--) {
        *uf = *buf->current;
        uf++;
        buf->current++;
    }
    return f;
}

struct byte_array* serial_decode_string(struct byte_array* buf)
{
	null_check(buf);
    int32_t len = serial_decode_int(buf);
	assert_message(len>=0, "negative malloc");
    struct byte_array* ba = byte_array_new_size(len);
    ba->data = ba->current = (uint8_t*)malloc(len);
	null_check(ba->data);
    memcpy(ba->data, buf->current, len);
    buf->current += len;
    return ba;
}

void serial_decode(struct byte_array* buf, serial_element se, const void* extra)
{
    while (buf->current < buf->data + buf->length)
    {
        // get key and wire type
        int32_t keyWire = serial_decode_int(buf);
        struct key_value_pair pair = {
            .key = keyWire >> 2,
            .wire_type = (enum serial_type)(keyWire & 0x03)
        };

        // get data
        switch(pair.wire_type) {
            case SERIAL_INT:  /* int */
                pair.value.integer = serial_decode_int(buf);
                break;
            case SERIAL_FLOAT:
                pair.value.floater = serial_decode_float(buf);
            case SERIAL_STRING:  /* bytes */
                pair.value.bytes = serial_decode_string(buf);
                break;
            case SERIAL_ARRAY:
                break;
            default:
                DEBUGPRINT("serial_decode ?\n");
                break;
        }
        if (se(&pair, buf, extra)) {
//            DEBUGPRINT("serial_decode: break\n");
            break;
        }
    }
//    DEBUGPRINT("serial_decode done\n");
}

// assume little endian
struct byte_array *encode_float(struct byte_array *buf, float f)
{
    assert_message(sizeof(float)==4, "bad float size");
    uint8_t *uf = (uint8_t*)&f;
    for (int i=4; i; i--) {
        byte_array_add_byte(buf, *uf);
        uf++;
    }
    return buf;
}

struct byte_array* serial_encode_float(struct byte_array* buf, float value) {
    if (!buf)
        buf = byte_array_new();
    encode_float(buf, value);
    return buf;
}

uint8_t serial_encode_string_size(int32_t key, const struct byte_array* string) {
    if (!string)
        return 0;
    return (key ? encode_int_size(key) : 0) +
           encode_int_size(string->length) +
           string->length;
}

struct byte_array* serial_encode_string(struct byte_array* buf, const struct byte_array* bytes)
{
    if (!bytes)
        return buf;
    if (!buf)
        buf = byte_array_new();

    encode_int(buf, bytes->length);
    byte_array_resize(buf, buf->length + bytes->length);
    memcpy(buf->current, bytes->data, bytes->length);

    buf->current = buf->data + buf->length;
    return buf;
}

#ifdef DEBUG

bool display_serial(const struct key_value_pair* kvp) {
    DEBUGPRINT("serialElementDebug %d:\t", kvp->key);
    char* str;

    switch (kvp->wire_type) {
        case SERIAL_ERROR:
            DEBUGPRINT("error %d\n", kvp->value.serialError);
            break;
        case SERIAL_INT:
            DEBUGPRINT("int %d\n", kvp->value.integer);
            break;
        case SERIAL_FLOAT:
            DEBUGPRINT("float %f\n", kvp->value.floater)
        case SERIAL_STRING:
            str = (char*)malloc(kvp->value.bytes->length + 1);
            memcpy(str, kvp->value.bytes->data, kvp->value.bytes->length);
            str[kvp->value.bytes->length] = 0;
            DEBUGPRINT("bytes %s\n", str);
            break;
        case SERIAL_ARRAY:
            DEBUGPRINT("array\n");
            break;            
    }
    return true;
}

#endif // DEBUG
