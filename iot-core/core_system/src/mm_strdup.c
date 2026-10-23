/**
 * @file mm_strdup.c
 * @brief String duplication (allocated with core_malloc, released by the
 *        caller with core_free).
 *
 * The declaration is in mix_method.h.
 *
 * @copyright Copyright (c) 2021-2024 Tuya Inc. All Rights Reserved.
 */
#include "core_memory.h"

#include <string.h>

/**
 * @brief Duplicates a string.
 *
 * @param str input string (NULL returns NULL)
 * @return heap pointer (released by the caller with core_free); NULL if the
 *         allocation fails
 */
char *mm_strdup(const char *str)
{
    if ((void *)0 == str) {
        return (void *)0;
    }

    char *tmp = core_malloc(strlen(str) + 1);
    if ((void *)0 == tmp) {
        return (void *)0;
    }
    memcpy(tmp, str, strlen(str));
    tmp[strlen(str)] = '\0';

    return tmp;
}
