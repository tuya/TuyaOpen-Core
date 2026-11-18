/**
 * @file pj_compat.h
 * @brief Minimal pjproject-compatible types for the libre ICE backend.
 *
 * tuya_media_service_rtc.c talks to the ICE layer through the pjproject-style
 * API in pj_ice.h. When CONFIG_P2P_ICE_LIBRE is set, pjproject is not built;
 * this header supplies only the types and helpers that header and its callers
 * actually use. It is not a pjlib replacement.
 *
 * Keep this list in sync with pj_ice.h and tuya_media_service_rtc.c. Anything
 * not listed here should be expressed through the neutral API instead of being
 * added ad hoc.
 */

#ifndef PJ_COMPAT_H_
#define PJ_COMPAT_H_

#include <errno.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>

typedef int pj_status_t;
typedef int pj_bool_t;
typedef size_t pj_size_t;
typedef uint8_t pj_uint8_t;
typedef uint16_t pj_uint16_t;
typedef uint32_t pj_uint32_t;

#define PJ_SUCCESS 0
#define PJ_TRUE 1
#define PJ_FALSE 0

/* Values only used for comparisons in the app; the exact numbers do not have
 * to match pjproject because both sides are compiled from this header. */
#define PJ_EPENDING (-70001)
#define PJ_ETOOSMALL (-70002)
#define PJ_ENOTFOUND (-70006)
#define PJ_ERR_MSG_SIZE 128
#define PJ_INET6_ADDRSTRLEN 46
#define PJ_ICE_MAX_CAND 32

#define PJ_AF_UNSPEC AF_UNSPEC
#define PJ_AF_INET AF_INET
#define PJ_AF_INET6 AF_INET6

/* pjproject exposes pj_AF_INET()/pj_AF_INET6() as functions; callers use the
 * lowercase form in expressions such as cand.addr.addr.sa_family ==
 * pj_AF_INET6(). */
static inline int pj_AF_UNSPEC(void)
{
    return AF_UNSPEC;
}

static inline int pj_AF_INET(void)
{
    return AF_INET;
}

static inline int pj_AF_INET6(void)
{
    return AF_INET6;
}

/* Logging is compiled out on the libre backend: pjproject's log level is 0 in
 * production, and the adapter has its own diagnostics. */
#define PJ_LOG(level, args) ((void)0)

typedef struct pj_str_t {
    char *ptr;
    size_t slen;
} pj_str_t;

static inline pj_str_t pj_str(char *str)
{
    pj_str_t s;

    s.ptr = str;
    s.slen = str ? strlen(str) : 0;
    return s;
}

/* Same shape as pj_sockaddr: callers index cand.addr.addr.sa_family. */
typedef union pj_sockaddr {
    struct sockaddr addr;
    struct sockaddr_in ipv4;
    struct sockaddr_in6 ipv6;
} pj_sockaddr;

typedef pj_sockaddr pj_sockaddr_t;

struct pj_pool_t;
typedef struct pj_pool_t pj_pool_t;

typedef enum pj_ice_cand_type {
    PJ_ICE_CAND_TYPE_HOST = 0,
    PJ_ICE_CAND_TYPE_SRFLX = 1,
    PJ_ICE_CAND_TYPE_PRFLX = 2,
    PJ_ICE_CAND_TYPE_RELAYED = 3
} pj_ice_cand_type;

typedef struct pj_ice_sess_cand {
    pj_str_t foundation;
    pj_uint8_t comp_id;
    pj_uint32_t prio;
    pj_sockaddr addr;
    pj_sockaddr base_addr;
    pj_ice_cand_type type;
    pj_uint32_t local_pref;
} pj_ice_sess_cand;

typedef struct pj_ice_sess_check {
    const pj_ice_sess_cand *lcand;
    const pj_ice_sess_cand *rcand;
} pj_ice_sess_check;

struct pj_ice_session;
typedef struct pj_ice_session pj_ice_session_t;

/* The transport handle passed to callbacks is the session itself; this alias
 * keeps the existing callback signatures compiling. */
typedef struct pj_ice_session pj_ice_strans;

typedef enum pj_ice_strans_op {
    PJ_ICE_STRANS_OP_INIT = 0,
    PJ_ICE_STRANS_OP_NEGOTIATION = 1,
    PJ_ICE_STRANS_OP_KEEP_ALIVE = 2
} pj_ice_strans_op;

typedef enum pj_ice_strans_state {
    PJ_ICE_STRANS_STATE_NULL = 0,
    PJ_ICE_STRANS_STATE_SESS_READY,
    PJ_ICE_STRANS_STATE_TSX_READY,
    PJ_ICE_STRANS_STATE_NEGO_READY,
    PJ_ICE_STRANS_STATE_RUNNING,
    PJ_ICE_STRANS_STATE_FAILED
} pj_ice_strans_state;

/* Implemented by the libre backend (libre_ice.c). */
const char *pj_ice_get_cand_type_name(pj_ice_cand_type type);
int pj_sockaddr_print(const pj_sockaddr_t *addr, char *buf, pj_size_t size,
                      unsigned flags);
int pj_strerror(pj_status_t status, char *buf, pj_size_t size);

void *pj_ice_strans_get_user_data(pj_ice_strans *ice_st);
const pj_ice_sess_check *pj_ice_strans_get_valid_pair(pj_ice_strans *ice_st,
                                                      unsigned comp_id);
pj_status_t pj_ice_strans_enum_cands(pj_ice_strans *ice_st, unsigned comp_id,
                                     unsigned *count,
                                     pj_ice_sess_cand cands[]);
pj_bool_t pj_ice_strans_has_sess(pj_ice_strans *ice_st);
pj_bool_t pj_ice_strans_sess_is_complete(pj_ice_strans *ice_st);
pj_ice_strans_state pj_ice_strans_get_state(pj_ice_strans *ice_st);

#endif /* PJ_COMPAT_H_ */
