#ifndef CHECK_H
#define CHECK_H

#include <stdio.h>

#define SOFT_ASSERT(cond, msg, ret) \
    do \
    { \
        if (!(cond)) \
        { \
            fprintf(stderr, "\nCondition " #cond " has failed\n"); \
            fprintf(stderr, "%s\n", (msg)); \
            return (ret); \
        } \
    } while (0)

#define SOFT_ASSERT_VOID(cond, msg) \
    do \
    { \
        if (!(cond)) \
        { \
            fprintf(stderr, "\nCondition " #cond " has failed\n"); \
            fprintf(stderr, "%s\n", (msg)); \
            return; \
        } \
    } while (0)

#endif
