/**
 * @file pj_tuyaos_local_ip.c
 * @brief Local IPv4 helper for pj: reports the interface address in network
 * byte order through CORE.
 * @version 1.2
 * @copyright Copyright (c) Tuya Inc.
 *
 * core_compat_get_sta_ipv4_nbo() reads the address with core_netif_get_ip()
 * when ENABLE_WIRED is set, parses the dotted string, and rejects loopback
 * and unspecified hosts. Without ENABLE_WIRED, or when no usable address is
 * available, it returns -1.
 */
#include "core_local_ip.h"
#include "tuya_cloud_types.h"
#include "tuya_iot_config.h"

#include <stdio.h>
#include <string.h>

#if defined(ENABLE_WIRED) && (ENABLE_WIRED == 1)
#include "core_netif.h"
#endif

/**
 * @brief Parse dotted IPv4 into network-byte-order u32
 * @param[in] s dotted string
 * @param[out] nbo network-order address
 * @return 0 on success, -1 on failure
 */
static int __parse_ipv4_nbo(const char *s, uint32_t *nbo)
{
    uint32_t a, b, c, d;
    uint8_t bytes[4];

    if (s == NULL || nbo == NULL) {
        return -1;
    }
    if (sscanf(s, "%u.%u.%u.%u", &a, &b, &c, &d) != 4) {
        return -1;
    }
    if (a > 255U || b > 255U || c > 255U || d > 255U) {
        return -1;
    }
    bytes[0] = (uint8_t)a;
    bytes[1] = (uint8_t)b;
    bytes[2] = (uint8_t)c;
    bytes[3] = (uint8_t)d;
    memcpy(nbo, bytes, sizeof(bytes));
    return 0;
}

/**
 * @brief Reject loopback / empty after parsing NBO address
 * @param[in] a network-order IPv4
 * @return 0 if usable, -1 if reject
 */
static int __reject_bad_host(uint32_t a)
{
    uint32_t host;
    uint8_t host_bytes[4];

    memcpy(host_bytes, &a, sizeof(host_bytes));
    host = ((uint32_t)host_bytes[0] << 24) | ((uint32_t)host_bytes[1] << 16) |
           ((uint32_t)host_bytes[2] << 8) | (uint32_t)host_bytes[3];
    if ((host >> 24) == 127U || (host >> 24) == 0U) {
        return -1;
    }
    return 0;
}

/**
 * @brief Get station/wired IPv4 in network byte order via CORE
 * @param[out] addr_nbo IPv4 address (network byte order)
 * @return 0 on success, -1 on failure / unavailable / loopback
 */
int core_compat_get_sta_ipv4_nbo(unsigned int *addr_nbo)
{
    NW_IP_S ip;
    uint32_t a;

    if (addr_nbo == NULL) {
        return -1;
    }

    memset(&ip, 0, sizeof(ip));

#if defined(ENABLE_WIRED) && (ENABLE_WIRED == 1)
    memset(&ip, 0, sizeof(ip));
    if (core_netif_get_ip(&ip) == OPRT_OK && ip.ip[0] != '\0') {
        if (__parse_ipv4_nbo(ip.ip, &a) == 0 && __reject_bad_host(a) == 0) {
            *addr_nbo = (unsigned int)a;
            return 0;
        }
    }
#endif

    (void)ip;
    (void)a;
    return -1;
}
