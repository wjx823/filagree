/* serial.h
 *
 *	[de]serialization API
 */

#ifndef SERIAL_H
#define SERIAL_H


#include <stdint.h> 
#include "struct.h"


enum serial_type {
	SERIAL_ERROR,
	SERIAL_INT,
	SERIAL_FLOAT,
	SERIAL_STRING,
	SERIAL_ARRAY,
};


struct key_value_pair
{
	int32_t key;

	enum serial_type wire_type;

	union {
        int32_t integer;
		float floater;
		struct byte_array* bytes;
		enum {
			SOMETHING_HAS_GONE_HORRIBLY_WRONG,
		} serialError;
    } value;
};


typedef bool (serial_element)(const struct key_value_pair*,
							  struct byte_array* bytes,
							  const void* extra);
bool serial_element_debug(const struct key_value_pair* kvp);

struct byte_array* serial_encode_int(struct byte_array* buf,
									  int32_t value);

struct byte_array *serial_encode_float(struct byte_array *buf,
									   float value);

struct byte_array* serial_encode_string(struct byte_array* buf,
										const struct byte_array* string);
void serial_decode(struct byte_array* buf,
				   serial_element* se,
				   const void* extra);

int32_t serial_decode_int(struct byte_array* buf);

float serial_decode_float(struct byte_array* buf);

struct byte_array* serial_decode_string(struct byte_array* buf);

#endif // SERIAL_H
