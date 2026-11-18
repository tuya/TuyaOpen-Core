/**
 * @file libre_compat.c
 * @brief ESP-IDF libc/lwIP fallbacks required when libre is linked in.
 *
 * These are link-time gaps of the ESP-IDF host, not P2P features:
 * - libre's mqueue.c references pipe(), although the P2P path never uses
 *   mqueue; ESP-IDF's newlib does not expose pipe() to this link, so return
 *   ENOSYS instead of pulling VFS plumbing into the image.
 * - ESP-IDF's lwIP exposes getaddrinfo() but not gai_strerror(); libre only
 *   uses it in debug logging, so a weak fallback is enough. A platform
 *   implementation, if one ever appears, still wins at link time.
 *
 * clock_gettime() is provided by the toolchain and must not be redefined.
 *
 * @copyright Copyright (c) 2026 Tuya Inc. All Rights Reserved.
 */
#include <errno.h>
#include <stddef.h>
#include <unistd.h>

int pipe(int fds[2])
{
    (void)fds;
    errno = ENOSYS;
    return -1;
}

__attribute__((weak)) const char *gai_strerror(int errcode)
{
    (void)errcode;
    return "getaddrinfo failed";
}
