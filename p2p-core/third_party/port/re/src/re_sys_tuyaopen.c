/**
 * @file re_sys_tuyaopen.c
 * @brief libre sys/rand glue for TuyaOpen platforms.
 *
 * Replaces libre's POSIX sleep.c and rand.c with the TuyaOpen core_system
 * primitives. This port therefore does not depend on usleep() or rand();
 * core_system_get_random() is backed by the platform RNG.
 *
 * @copyright Copyright (c) 2026 Tuya Inc. All Rights Reserved.
 */
#include <stddef.h>
#include <stdint.h>

#include <re_types.h>
#include <re_sys.h>

#include "core_system.h"

static const char alphanum[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    "abcdefghijklmnopqrstuvwxyz"
    "0123456789";

void sys_usleep(unsigned int us)
{
    uint32_t ms;

    if (us == 0) {
        return;
    }

    ms = (us + 999U) / 1000U;
    if (ms == 0) {
        ms = 1;
    }
    core_system_sleep(ms);
}

uint32_t rand_u32(void)
{
    uint32_t v;

    /* core_system_get_random() is inclusive; use 31 bits and one extra bit
     * rather than relying on a full-range int argument. */
    v = (uint32_t)core_system_get_random(0x7fffffffU) << 1;
    v |= (uint32_t)core_system_get_random(1U);
    return v;
}

uint16_t rand_u16(void)
{
    return (uint16_t)(rand_u32() >> 16);
}

uint64_t rand_u64(void)
{
    return ((uint64_t)rand_u32() << 32) | rand_u32();
}

void rand_bytes(uint8_t *p, size_t size)
{
    size_t i;

    if (p == NULL) {
        return;
    }
    for (i = 0; i < size; i++) {
        p[i] = (uint8_t)core_system_get_random(255U);
    }
}

void rand_str(char *str, size_t size)
{
    size_t i;

    if (str == NULL || size == 0) {
        return;
    }

    --size;
    rand_bytes((uint8_t *)str, size);
    for (i = 0; i < size; i++) {
        str[i] = alphanum[((uint8_t)str[i]) % (sizeof(alphanum) - 1)];
    }
    str[size] = '\0';
}

char rand_char(void)
{
    char s[2];

    rand_str(s, sizeof(s));
    return s[0];
}


