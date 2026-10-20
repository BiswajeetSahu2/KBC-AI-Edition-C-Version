#ifndef KBC_TYPES_H
#define KBC_TYPES_H

typedef struct {
    int active;
    char id[32], player[64], mode[24], subjects[5][64];
    int total;
} Game;

typedef struct {
    const char *subject, *question, *a, *b, *c, *d, *answer, *explanation;
} Question;

#endif