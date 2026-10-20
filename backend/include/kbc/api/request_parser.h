#ifndef KBC_REQUEST_PARSER_H
#define KBC_REQUEST_PARSER_H

#include <stddef.h>

const char *json_value(const char *source, const char *key);
int json_string(const char *source, const char *key, char *output, size_t capacity);
int json_integer(const char *source, const char *key, long *value);
int json_subjects(const char *source, char output[5][64]);
int json_escape(const char *source, char *output, size_t capacity);
int query_value(const char *path, const char *key, char *output, size_t capacity);

#endif