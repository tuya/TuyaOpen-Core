/**
 * @file core_local_ip.h
 * @brief Local IPv4 helper for pj on TuyaOpen (wired)
 * @note Used when PJ_TUYAOS=1 (LINUX and MCU).
 */
#ifndef __CORE_LOCAL_IP_H__
#define __CORE_LOCAL_IP_H__

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Get local IPv4 in network byte order (wired)
 * @param[out] addr_nbo IPv4 network byte order
 * @return 0 on success, -1 on failure
 */
int core_compat_get_sta_ipv4_nbo(unsigned int *addr_nbo);

#ifdef __cplusplus
}
#endif

#endif /* __CORE_LOCAL_IP_H__ */
