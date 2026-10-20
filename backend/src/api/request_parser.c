#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "kbc/api/request_parser.h"

const char *json_value(const char *source, const char *key) {
    char pattern[96];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    const char *position = strstr(source, pattern);
    if (!position) return NULL;
    position = strchr(position + strlen(pattern), ':');
    if (!position) return NULL;
    do { position++; } while (*position && isspace((unsigned char)*position));
    return position;
}

int json_string(const char *source, const char *key, char *output, size_t capacity) {
    const char *position = json_value(source, key);
    if (!position || *position != '"') return 0;
    position++;
    size_t length = 0;
    while (*position && *position != '"') {
        unsigned char character = (unsigned char)*position++;
        if (character == '\\') {
            if (!*position) return 0;
            character = (unsigned char)*position++;
            if (character == 'n') character = '\n';
            else if (character == 'r') character = '\r';
            else if (character == 't') character = '\t';
            else if (character == 'b') character = '\b';
            else if (character == 'f') character = '\f';
            else if (character != '"' && character != '\\' && character != '/') return 0;
        }
        if (!character || length + 1 >= capacity) return 0;
        output[length++] = (char)character;
    }
    if (*position != '"' || !length) return 0;
    output[length] = '\0';
    return 1;
}

int json_integer(const char *source, const char *key, long *value) {
    const char *position = json_value(source, key);
    if (!position) return 0;
    char *end = NULL;
    long parsed = strtol(position, &end, 10);
    if (end == position || (*end && *end != ',' && *end != '}' && !isspace((unsigned char)*end))) return 0;
    *value = parsed;
    return 1;
}

int json_escape(const char *source, char *output, size_t capacity) {
    size_t length = 0;
    for (const unsigned char *character = (const unsigned char *)source; *character; character++) {
        const char *escape = NULL;
        if (*character == '"') escape = "\\\"";
        else if (*character == '\\') escape = "\\\\";
        else if (*character == '\n') escape = "\\n";
        else if (*character == '\r') escape = "\\r";
        else if (*character == '\t') escape = "\\t";
        else if (*character < 0x20) escape = " ";
        if (escape) {
            size_t escape_length = strlen(escape);
            if (length + escape_length >= capacity) return 0;
            memcpy(output + length, escape, escape_length);
            length += escape_length;
        } else {
            if (length + 1 >= capacity) return 0;
            output[length++] = (char)*character;
        }
    }
    output[length] = '\0';
    return 1;
}

int json_subjects(const char *source, char output[5][64]) {
    const char *position = json_value(source, "subjects");
    if (!position || *position++ != '[') return 0;
    int count = 0;
    while (*position) {
        while (*position && (isspace((unsigned char)*position) || *position == ',')) position++;
        if (*position == ']') break;
        if (*position != '"' || count == 5) return 0;
        const char *start = position++;
        int escaped = 0;
        while (*position && (*position != '"' || escaped)) {
            if (!escaped && *position == '\\') escaped = 1;
            else escaped = 0;
            position++;
        }
        if (*position != '"') return 0;
        char encoded[128];
        size_t length = (size_t)(position - start + 1);
        if (length >= sizeof(encoded)) return 0;
        memcpy(encoded, start, length);
        encoded[length] = '\0';
        char wrapper[144];
        snprintf(wrapper, sizeof(wrapper), "{\"subject\":%s}", encoded);
        if (!json_string(wrapper, "subject", output[count], 64)) return 0;
        count++;
        position++;
    }
    return count == 5;
}

int query_value(const char *path, const char *key, char *output, size_t capacity) {
    char pattern[64];
    snprintf(pattern, sizeof(pattern), "%s=", key);
    const char *position = strchr(path, '?');
    if (!position) return 0;
    position++;
    while (*position) {
        const char *end = strchr(position, '&');
        size_t segment_length = end ? (size_t)(end - position) : strlen(position);
        size_t prefix_length = strlen(pattern);
        if (segment_length >= prefix_length && !strncmp(position, pattern, prefix_length)) {
            size_t length = segment_length - prefix_length;
            if (!length || length >= capacity) return 0;
            memcpy(output, position + prefix_length, length);
            output[length] = '\0';
            return 1;
        }
        if (!end) break;
        position = end + 1;
    }
    return 0;
}