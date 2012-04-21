//
//  interpret.h
//  filagree
//

#ifndef INTERPRET_H
#define INTERPRET_H

struct variable *interpret_file(const struct byte_array *filename, bridge *callback);
struct variable *interpret_string(const char *str, bridge *callback);

#endif // INTERPRET_H
