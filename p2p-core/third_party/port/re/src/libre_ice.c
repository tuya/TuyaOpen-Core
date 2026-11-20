/**
 * @file libre_ice.c
 * @brief libre (baresip/re) implementation of the pj_ice.h adapter API.
 *
 * Replaces pjproject's pj_ice_strans for ICE/STUN (UDP). The adapter keeps the
 * existing pjproject-style surface so tuya_media_service_rtc.c does not need a
 * protocol rewrite:
 *
 *   create/init      parse STUN/TURN tokens on the signaling thread
 *   first handle_events  create the UDP socket + icem on rtc_wrk and start
 *                        candidate gathering (libre objects are created lazily
 *                        so every libre call and re_main() runs in one thread)
 *   handle_events    pump libre's event loop for max_msec (rtc_wrk calls every
 *                    5 ms, mirroring the old pj_ioqueue_poll cadence)
 *   sendto           queue the datagram; handle_events flushes it on rtc_wrk
 *
 * Candidate strings are encoded with libre and parsed back into
 * pj_ice_sess_cand, so the app's print_cand/parse_cand contract is unchanged.
 */

#include "pj_ice.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <re.h>

#include "cJSON.h"
#include "core_mutex.h"

#define ICE_LOG(...) re_fprintf(stderr, __VA_ARGS__)

#define ICE_COMP_ID 1

#define ICE_MAX_PENDING_CANDS 16
#define ICE_UDP_RXSZ 1500
#define ICE_TURN_LIFETIME 600

struct ice_send_item {
    struct ice_send_item *next;
    size_t len;
    uint8_t data[];
};

struct pj_ice_session {
    pj_ice_session_cfg_t cfg;
    char local_ufrag[64];
    char local_passwd[128];

    volatile bool init_called;
    volatile bool setup_done;
    volatile bool gather_done;
    volatile bool gather_failed;
    volatile bool local_gather_done;
    volatile bool start_requested;
    volatile bool remote_ready;
    volatile bool conncheck_started;
    volatile bool nego_done;
    volatile bool nego_success;
    volatile bool destroyed;

    enum ice_role role;
    uint64_t tiebrk;
    struct icem *icem;
    struct udp_sock *us;
    struct sa laddr;
    char ifname[16];

    struct sa stun_srv;
    bool has_stun;
    bool stun_fallback;
    bool stun_started;

    struct sa turn_srv;
    char turn_user[128];
    char turn_pass[128];
    bool has_turn;
    bool turn_done;
    struct turnc *turnc;
    unsigned gather_pending;

    /* signaling thread -> rtc_wrk */
    char rufrag[256];
    char rpwd[256];
    bool has_rufrag;
    bool has_rpwd;
    bool remote_ufrag_set;
    bool remote_pwd_set;
    char pending_cands[ICE_MAX_PENDING_CANDS][256];
    unsigned pending_cand_cnt;
    /* Last state printed while waiting for icem; the pump calls
     * apply_pending_remote() every few ms, so log only on change. */
    unsigned wait_log_pending;
    bool wait_log_ufrag;
    bool wait_log_pwd;

    /* stable candidate cache for enum_cands / get_valid_pair */
    pj_ice_sess_cand lcands[PJ_ICE_MAX_CAND];
    char lcand_foundation[PJ_ICE_MAX_CAND][32];
    unsigned lcand_cnt;

    pj_ice_sess_cand valid_lcand;
    pj_ice_sess_cand valid_rcand;
    char valid_lfoundation[32];
    char valid_rfoundation[32];
    pj_ice_sess_check valid_check;
    bool has_valid_pair;

    /* outgoing datagrams from any thread */
    struct ice_send_item *send_head;
    struct ice_send_item *send_tail;
    MUTEX_HANDLE lock;

    struct tmr pump_tmr;
};

static bool g_libre_inited;
static unsigned g_libre_users;

static void pump_cancel(void *arg)
{
    (void)arg;
    re_cancel();
}

static int global_init(void)
{
    int err;

    if (g_libre_inited)
        return 0;

    /* A host application (or a test) may already have called libre_init() on
     * this thread; reuse its re context instead of initialising twice. */
    if (re_thread_check(false) == 0) {
        (void)fd_setsize(64);
        g_libre_inited = true;
        return 0;
    }

    err = libre_init();
    if (err)
        return err;

    /* 64 fds is far above the one UDP socket this stack needs; it keeps the
     * per-context event arrays small (default is FD_SETSIZE). */
    (void)fd_setsize(64);

    g_libre_inited = true;
    return 0;
}

static void global_deinit(void)
{
    if (!g_libre_inited || g_libre_users > 0)
        return;

    /* Only the thread that called libre_init() owns the re context. Session
     * create/destroy run on the signaling thread, so this normally matches. */
    if (re_thread_check(false) == 0)
        libre_close();
    g_libre_inited = false;
}

/* ------------------------------------------------------------------------- */
/* pj_sockaddr / candidate helpers                                           */

static uint16_t sockaddr_port(const pj_sockaddr_t *addr)
{
    if (addr->addr.sa_family == AF_INET)
        return ntohs(addr->ipv4.sin_port);
    if (addr->addr.sa_family == AF_INET6)
        return ntohs(addr->ipv6.sin6_port);
    return 0;
}

static bool sockaddr_valid(const pj_sockaddr_t *addr)
{
    return addr->addr.sa_family == AF_INET || addr->addr.sa_family == AF_INET6;
}

static bool sockaddr_equal(const pj_sockaddr_t *a, const pj_sockaddr_t *b)
{
    if (a->addr.sa_family != b->addr.sa_family)
        return false;

    if (a->addr.sa_family == AF_INET) {
        return a->ipv4.sin_port == b->ipv4.sin_port &&
               a->ipv4.sin_addr.s_addr == b->ipv4.sin_addr.s_addr;
    }
    if (a->addr.sa_family == AF_INET6) {
        return a->ipv6.sin6_port == b->ipv6.sin6_port &&
               memcmp(&a->ipv6.sin6_addr, &b->ipv6.sin6_addr,
                      sizeof(a->ipv6.sin6_addr)) == 0;
    }
    return false;
}

static int sockaddr_set_ip(pj_sockaddr *addr, const char *ip)
{
    struct sockaddr_in *sin = &addr->ipv4;
    struct sockaddr_in6 *sin6 = &addr->ipv6;

    memset(addr, 0, sizeof(*addr));
    if (inet_pton(AF_INET, ip, &sin->sin_addr) == 1) {
        sin->sin_family = AF_INET;
        addr->addr.sa_family = AF_INET;
        return 0;
    }
    if (inet_pton(AF_INET6, ip, &sin6->sin6_addr) == 1) {
        sin6->sin6_family = AF_INET6;
        addr->addr.sa_family = AF_INET6;
        return 0;
    }
    return -1;
}

static void sockaddr_set_port_value(pj_sockaddr *addr, uint16_t port)
{
    if (addr->addr.sa_family == AF_INET)
        addr->ipv4.sin_port = htons(port);
    else if (addr->addr.sa_family == AF_INET6)
        addr->ipv6.sin6_port = htons(port);
}

int pj_sockaddr_print(const pj_sockaddr_t *addr, char *buf, pj_size_t size,
                      unsigned flags)
{
    char ip[PJ_INET6_ADDRSTRLEN];
    int n;

    if (!addr || !buf || !size)
        return -1;

    if (!sockaddr_valid(addr)) {
        if (size < 3)
            return -1;
        strcpy(buf, "--");
        return 2;
    }

    if (addr->addr.sa_family == AF_INET) {
        if (!inet_ntop(AF_INET, &addr->ipv4.sin_addr, ip, sizeof(ip)))
            return -1;
    } else {
        if (!inet_ntop(AF_INET6, &addr->ipv6.sin6_addr, ip, sizeof(ip)))
            return -1;
    }

    if ((flags & 2) && addr->addr.sa_family == AF_INET6)
        n = snprintf(buf, size, "[%s]", ip);
    else
        n = snprintf(buf, size, "%s", ip);

    if (n < 0 || (pj_size_t)n >= size)
        return -1;

    if (flags & 1) {
        int n2 = snprintf(buf + n, size - (pj_size_t)n, ":%u",
                          (unsigned)sockaddr_port(addr));
        if (n2 < 0 || (pj_size_t)(n + n2) >= size)
            return -1;
        n += n2;
    }

    return n;
}

const char *pj_ice_get_cand_type_name(pj_ice_cand_type type)
{
    switch (type) {
    case PJ_ICE_CAND_TYPE_HOST:
        return "host";
    case PJ_ICE_CAND_TYPE_SRFLX:
        return "srflx";
    case PJ_ICE_CAND_TYPE_PRFLX:
        return "prflx";
    case PJ_ICE_CAND_TYPE_RELAYED:
        return "relay";
    default:
        return "?";
    }
}

int pj_strerror(pj_status_t status, char *buf, pj_size_t size)
{
    if (!buf || !size)
        return -1;

    if (status >= 0) {
        snprintf(buf, size, "%s", strerror(status));
    } else {
        snprintf(buf, size, "ICE error %d", status);
    }
    return 0;
}

/* Candidate line -> pj_ice_sess_cand. foundation points into orig_input. */
int parse_cand(pj_pool_t *pool, const pj_str_t *orig_input,
               pj_ice_sess_cand *cand)
{
    char tmp[512];
    char *save = NULL;
    char *tok;
    char *foundation;
    size_t len;
    size_t prefix = 0;
    unsigned idx = 0;

    (void)pool;

    if (!orig_input || !orig_input->ptr || !orig_input->slen || !cand)
        return -1;

    len = orig_input->slen;
    if (len >= sizeof(tmp))
        len = sizeof(tmp) - 1;
    memcpy(tmp, orig_input->ptr, len);
    tmp[len] = '\0';

    while (len > 0 && (tmp[len - 1] == '\r' || tmp[len - 1] == '\n' ||
                       isspace((unsigned char)tmp[len - 1])))
        tmp[--len] = '\0';

    memset(cand, 0, sizeof(*cand));

    /* The signaling path carries the full "a=candidate:" attribute; libre's
     * SDP decoder expects the value only, but the app's parse_cand is called
     * with either form. Accept both. */
    if (len >= 12 && strncasecmp(tmp, "a=candidate:", 12) == 0)
        prefix = 12;

    foundation = strtok_r(tmp + prefix, " ", &save);
    if (!foundation)
        return -1;

    /* foundation must stay valid after this function returns; point it back
     * into the caller's buffer. */
    cand->foundation.ptr = orig_input->ptr + prefix;
    cand->foundation.slen = strlen(foundation);
    if (cand->foundation.slen + prefix > orig_input->slen)
        return -1;

    for (idx = 0; idx < 7; ++idx) {
        char *end = NULL;
        unsigned long v;

        tok = strtok_r(NULL, " ", &save);
        if (!tok)
            return -1;

        switch (idx) {
        case 0: /* component id */
            v = strtoul(tok, &end, 10);
            if (!end || *end || v == 0 || v > 255)
                return -1;
            cand->comp_id = (pj_uint8_t)v;
            break;
        case 1: /* transport */
            if (strcasecmp(tok, "UDP") != 0)
                return -1;
            break;
        case 2: /* priority */
            v = strtoul(tok, &end, 10);
            if (!end || *end)
                return -1;
            cand->prio = (pj_uint32_t)v;
            break;
        case 3: /* address */
            if (sockaddr_set_ip(&cand->addr, tok) != 0)
                return -1;
            break;
        case 4: /* port */
            v = strtoul(tok, &end, 10);
            if (!end || *end || v > 65535)
                return -1;
            sockaddr_set_port_value(&cand->addr, (uint16_t)v);
            break;
        case 5: /* "typ" */
            if (strcasecmp(tok, "typ") != 0)
                return -1;
            break;
        case 6: /* candidate type */
            if (strcasecmp(tok, "host") == 0)
                cand->type = PJ_ICE_CAND_TYPE_HOST;
            else if (strcasecmp(tok, "srflx") == 0)
                cand->type = PJ_ICE_CAND_TYPE_SRFLX;
            else if (strcasecmp(tok, "prflx") == 0)
                cand->type = PJ_ICE_CAND_TYPE_PRFLX;
            else if (strcasecmp(tok, "relay") == 0)
                cand->type = PJ_ICE_CAND_TYPE_RELAYED;
            else
                return -1;
            break;
        default:
            break;
        }
    }

    /* Optional attributes. rport may appear after raddr (and vice versa). */
    {
        uint16_t rel_port = 0;
        bool has_rel_port = false;

        while ((tok = strtok_r(NULL, " ", &save)) != NULL) {
            if (strcasecmp(tok, "raddr") == 0) {
                char *val = strtok_r(NULL, " ", &save);
                if (!val || sockaddr_set_ip(&cand->base_addr, val) != 0)
                    return -1;
            } else if (strcasecmp(tok, "rport") == 0) {
                char *val = strtok_r(NULL, " ", &save);
                char *end = NULL;
                unsigned long v;

                if (!val)
                    return -1;
                v = strtoul(val, &end, 10);
                if (!end || *end || v > 65535)
                    return -1;
                rel_port = (uint16_t)v;
                has_rel_port = true;
            } else if (strcasecmp(tok, "tcptype") == 0) {
                (void)strtok_r(NULL, " ", &save);
            }
            /* ignore other trailing attributes */
        }

        if (has_rel_port)
            sockaddr_set_port_value(&cand->base_addr, rel_port);
    }

    return 0;
}

int print_cand(char buffer[], unsigned maxlen, const pj_ice_sess_cand *cand)
{
    char ip[PJ_INET6_ADDRSTRLEN];
    char base[PJ_INET6_ADDRSTRLEN];
    uint32_t prio;
    int n;

    if (!buffer || !maxlen || !cand)
        return -1;

    prio = cand->prio;
    if (prio == 0 && cand->type <= PJ_ICE_CAND_TYPE_RELAYED) {
        static const uint32_t type_prefs[] = {126, 100, 110, 0};
        uint32_t local_pref = cand->local_pref ? cand->local_pref : 65535;
        prio = ((type_prefs[cand->type] & 0xFFu) << 24) |
               ((local_pref & 0xFFFFu) << 8) |
               ((256u - cand->comp_id) & 0xFFu);
    }

    if (pj_sockaddr_print(&cand->addr, ip, sizeof(ip), 0) < 0)
        return -1;

    n = snprintf(buffer, maxlen, "a=candidate:%.*s %u UDP %u %s %u typ %s",
                 (int)cand->foundation.slen, cand->foundation.ptr,
                 (unsigned)cand->comp_id, (unsigned)prio, ip,
                 (unsigned)sockaddr_port(&cand->addr),
                 pj_ice_get_cand_type_name(cand->type));
    if (n < 0 || (unsigned)n >= maxlen)
        return -1;

    if ((cand->type == PJ_ICE_CAND_TYPE_SRFLX ||
         cand->type == PJ_ICE_CAND_TYPE_RELAYED) &&
        sockaddr_valid(&cand->base_addr)) {
        int n2;
        if (pj_sockaddr_print(&cand->base_addr, base, sizeof(base), 0) < 0)
            return -1;
        n2 = snprintf(buffer + n, maxlen - (unsigned)n, " raddr %s rport %u\r\n",
                      base, (unsigned)sockaddr_port(&cand->base_addr));
        if (n2 < 0 || (unsigned)(n + n2) >= maxlen)
            return -1;
        n += n2;
    } else {
        int n2 = snprintf(buffer + n, maxlen - (unsigned)n, "\r\n");
        if (n2 < 0 || (unsigned)(n + n2) >= maxlen)
            return -1;
        n += n2;
    }

    return n;
}

/* ------------------------------------------------------------------------- */
/* libre candidate -> pj candidate                                            */

/* Parse "stun:host:port" / "turn:[v6]:port" (query already stripped). */
static int ice_parse_server_url(const char *url, const char *scheme,
                                char *host, size_t hostlen, uint16_t *port)
{
    const char *p;
    const char *end;
    const char *colon = NULL;
    const char *i;
    size_t n;

    if (!url || !scheme || !host || !hostlen || !port)
        return -1;

    p = url + strlen(scheme);
    end = strchr(p, '?');
    if (!end)
        end = p + strlen(p);

    *port = 3478;

    if (p < end && *p == '[') {
        const char *rb = strchr(p, ']');
        if (!rb || rb >= end)
            return -1;
        n = (size_t)(rb - p - 1);
        if (n >= hostlen)
            return -1;
        memcpy(host, p + 1, n);
        host[n] = '\0';
        if (rb + 1 < end && rb[1] == ':')
            *port = (uint16_t)strtoul(rb + 2, NULL, 10);
        return *port ? 0 : -1;
    }

    for (i = p; i < end; ++i) {
        if (*i == ':')
            colon = i;
    }

    if (colon) {
        n = (size_t)(colon - p);
        if (n >= hostlen)
            return -1;
        memcpy(host, p, n);
        host[n] = '\0';
        *port = (uint16_t)strtoul(colon + 1, NULL, 10);
    } else {
        n = (size_t)(end - p);
        if (n >= hostlen)
            return -1;
        memcpy(host, p, n);
        host[n] = '\0';
    }

    return *port ? 0 : -1;
}

static int cand_from_libre(const struct ice_cand *ic, pj_ice_sess_cand *out,
                           char *foundation, size_t foundation_size)
{
    char line[256];
    pj_str_t str;
    pj_ice_sess_cand tmp;
    size_t flen;
    int n;

    n = re_snprintf(line, sizeof(line), "%H", ice_cand_encode, ic);
    if (n < 0)
        return ENOMEM;

    str = pj_str(line);
    if (parse_cand(NULL, &str, &tmp) != 0) {
        ICE_LOG("libre_ice: cannot parse encoded candidate '%s'\n", line);
        return EPROTO;
    }

    flen = tmp.foundation.slen;
    if (flen >= foundation_size)
        flen = foundation_size - 1;
    memcpy(foundation, tmp.foundation.ptr, flen);
    foundation[flen] = '\0';

    *out = tmp;
    out->foundation.ptr = foundation;
    out->foundation.slen = flen;
    return 0;
}

/* Remote candidates added through icem_sdp_decode() do not carry
 * cand->transp, so ice_cand_encode() renders the transport as "???" and the
 * line cannot be parsed back. The app only needs the address/type of the
 * selected remote candidate, so fill those fields directly. */
static void cand_from_libre_remote(const struct ice_cand *ic,
                                   pj_ice_sess_cand *out, char *foundation,
                                   size_t foundation_size)
{
    const struct sa *addr;
    const char *f = "remote";
    size_t flen = strlen(f);

    memset(out, 0, sizeof(*out));
    out->comp_id = (pj_uint8_t)ICE_COMP_ID;
    out->type = (pj_ice_cand_type)icem_cand_type(ic);
    out->prio = 0;

    addr = icem_lcand_addr(ic);
    if (addr && addr->len <= sizeof(out->addr))
        memcpy(&out->addr, &addr->u.sa, addr->len);

    if (flen >= foundation_size)
        flen = foundation_size - 1;
    memcpy(foundation, f, flen);
    foundation[flen] = '\0';
    out->foundation.ptr = foundation;
    out->foundation.slen = flen;
}

static bool cache_has(const struct pj_ice_session *s,
                      const pj_ice_sess_cand *cand)
{
    unsigned i;

    for (i = 0; i < s->lcand_cnt; ++i) {
        const pj_ice_sess_cand *c = &s->lcands[i];
        if (c->type == cand->type && c->comp_id == cand->comp_id &&
            sockaddr_equal(&c->addr, &cand->addr))
            return true;
    }
    return false;
}

static void cache_add(struct pj_ice_session *s, const pj_ice_sess_cand *cand)
{
    unsigned i;
    size_t flen;

    if (s->lcand_cnt >= PJ_ICE_MAX_CAND || cache_has(s, cand))
        return;

    i = s->lcand_cnt++;
    s->lcands[i] = *cand;
    flen = cand->foundation.slen;
    if (flen >= sizeof(s->lcand_foundation[i]))
        flen = sizeof(s->lcand_foundation[i]) - 1;
    memcpy(s->lcand_foundation[i], cand->foundation.ptr, flen);
    s->lcand_foundation[i][flen] = '\0';
    s->lcands[i].foundation.ptr = s->lcand_foundation[i];
    s->lcands[i].foundation.slen = flen;
}

static void cache_clear(struct pj_ice_session *s)
{
    s->lcand_cnt = 0;
}

static void cache_rebuild(struct pj_ice_session *s)
{
    struct le *le;

    cache_clear(s);
    for (le = icem_lcandl(s->icem)->head; le; le = le->next) {
        struct ice_cand *ic = le->data;
        pj_ice_sess_cand c;
        char foundation[32];

        if (cand_from_libre(ic, &c, foundation, sizeof(foundation)) == 0)
            cache_add(s, &c);
    }
}

static void emit_candidate(struct pj_ice_session *s,
                           const struct ice_cand *ic, bool last)
{
    pj_ice_sess_cand c;
    char foundation[32];

    if (!s->cfg.cb.ice_on_new_candidate)
        return;
    if (cand_from_libre(ic, &c, foundation, sizeof(foundation)) != 0)
        return;

    cache_add(s, &c);
    s->cfg.cb.ice_on_new_candidate((pj_ice_strans *)s, &c,
                                   last ? PJ_TRUE : PJ_FALSE);
}

/* Emit every local candidate not already sent. */
static void emit_new_local_candidates(struct pj_ice_session *s)
{
    struct le *le;

    for (le = icem_lcandl(s->icem)->head; le; le = le->next) {
        struct ice_cand *ic = le->data;
        pj_ice_sess_cand c;
        char foundation[32];

        if (cand_from_libre(ic, &c, foundation, sizeof(foundation)) != 0)
            continue;
        if (cache_has(s, &c))
            continue;
        emit_candidate(s, ic, false);
    }
}

/* ------------------------------------------------------------------------- */
/* gathering                                                                 */

static void start_conncheck(struct pj_ice_session *s);

static void gather_maybe_done(struct pj_ice_session *s)
{
    if (s->gather_pending > 0 || s->gather_done)
        return;

    icem_cand_redund_elim(s->icem);
    (void)icem_comps_set_default_cand(s->icem);
    cache_rebuild(s);

    s->gather_done = true;

    /* NULL + last tells the app to enumerate the final candidate set. */
    if (s->cfg.cb.ice_on_new_candidate)
        s->cfg.cb.ice_on_new_candidate((pj_ice_strans *)s, NULL, PJ_TRUE);

    /* A TURN-only gather can fail before any host/srflx candidate exists;
     * report INIT failure instead of "success with zero candidates". */
    if (s->lcand_cnt == 0) {
        s->gather_failed = true;
        ICE_LOG("libre_ice: gather completed with no candidates\n");
        if (s->cfg.cb.ice_on_ice_complete)
            s->cfg.cb.ice_on_ice_complete((pj_ice_strans *)s,
                                          PJ_ICE_STRANS_OP_INIT,
                                          PJ_ENOTFOUND);
        return;
    }

    if (s->cfg.cb.ice_on_ice_complete)
        s->cfg.cb.ice_on_ice_complete((pj_ice_strans *)s,
                                      PJ_ICE_STRANS_OP_INIT, PJ_SUCCESS);

    start_conncheck(s);
}

static void stun_resp_handler(int err, uint16_t scode, const char *reason,
                              const struct stun_msg *msg, void *arg)
{
    struct pj_ice_session *s = arg;
    struct stun_attr *attr;
    struct ice_cand *base;

    (void)reason;

    if (err || scode) {
        ICE_LOG("libre_ice: STUN binding failed err=%d scode=%u\n", err, scode);
        s->gather_failed = true;
        goto out;
    }

    attr = stun_msg_attr(msg, STUN_ATTR_XOR_MAPPED_ADDR);
    if (!attr)
        attr = stun_msg_attr(msg, STUN_ATTR_MAPPED_ADDR);
    if (!attr) {
        ICE_LOG("libre_ice: STUN response without mapped address\n");
        s->gather_failed = true;
        goto out;
    }

    base = icem_cand_find(icem_lcandl(s->icem), ICE_COMP_ID, NULL);
    if (!base) {
        ICE_LOG("libre_ice: no host candidate for srflx base\n");
        s->gather_failed = true;
        goto out;
    }

    if (icem_lcand_add(s->icem, icem_lcand_base(base), ICE_CAND_TYPE_SRFLX,
                       &attr->v.sa) != 0) {
        ICE_LOG("libre_ice: add srflx candidate failed\n");
        s->gather_failed = true;
        goto out;
    }

    emit_new_local_candidates(s);

 out:
    if (s->gather_pending > 0)
        s->gather_pending--;
    gather_maybe_done(s);
}

static int start_stun_gather(struct pj_ice_session *s)
{
    struct stun_ctrans *ct = NULL;
    int err;

    /* Mark the attempt even on failure so session_setup/fallback cannot start
     * a second Binding transaction for the same server. */
    s->stun_started = true;
    ICE_LOG("libre_ice: STUN gather %J\n", &s->stun_srv);
    err = stun_request(&ct, icem_stun(s->icem), IPPROTO_UDP, s->us,
                       &s->stun_srv, 0, STUN_METHOD_BINDING, NULL, 0, false,
                       stun_resp_handler, s, 1, STUN_ATTR_SOFTWARE,
                       stun_software);
    if (err) {
        ICE_LOG("libre_ice: STUN request failed: %d\n", err);
        return err;
    }
    return 0;
}

static void turn_fail_fallback(struct pj_ice_session *s)
{
    /* Remove the TURN helper before starting STUN: STUN and TURN are often
     * the same server address, and the TURN client would otherwise consume
     * the Binding response. */
    if (s->turnc) {
        mem_deref(s->turnc);
        s->turnc = NULL;
    }
    s->has_turn = false;

    /* relay_only was applied at icem_alloc time, so the host candidate added
     * in session_setup was silently dropped (libre cand.c filters non-RELAY
     * candidates under ICE_POLICY_RELAY). Clear the policy and re-add the host
     * base before falling back; otherwise STUN has no base and INIT "succeeds"
     * with zero candidates. */
    if (s->icem) {
        icem_conf(s->icem)->policy = ICE_POLICY_ALL;
        if (icem_lcand_add_base(s->icem, ICE_CAND_TYPE_HOST, ICE_COMP_ID, 0,
                                "tuya", ICE_TRANSP_UDP, &s->laddr) == 0) {
            emit_new_local_candidates(s);
        } else {
            ICE_LOG("libre_ice: failed to re-add host candidate on fallback\n");
            s->gather_failed = true;
        }
    }

    if (s->stun_fallback && s->has_stun && !s->stun_started) {
        s->stun_fallback = false;
        ICE_LOG("libre_ice: falling back to STUN\n");
        if (start_stun_gather(s) == 0)
            return; /* gather_pending remains 1 for STUN */
        s->gather_failed = true;
    }

    if (s->gather_pending > 0)
        s->gather_pending--;
    gather_maybe_done(s);
}

static void turnc_handler(int err, uint16_t scode, const char *reason,
                          const struct sa *relay_addr,
                          const struct sa *mapped_addr,
                          const struct stun_msg *msg, void *arg)
{
    struct pj_ice_session *s = arg;

    (void)msg;

    /* turnc also invokes the handler when a later refresh fails; only the
     * first allocation result participates in gathering. */
    if (s->turn_done) {
        if (err || scode)
            ICE_LOG("libre_ice: TURN refresh failed err=%d scode=%u\n", err,
                    scode);
        return;
    }
    s->turn_done = true;

    if (err || scode || !relay_addr) {
        ICE_LOG("libre_ice: TURN allocate failed err=%d scode=%u (%s)\n", err,
                scode, reason ? reason : "?");
        s->gather_failed = true;
        turn_fail_fallback(s);
        return;
    }

    ICE_LOG("libre_ice: TURN allocated relay=%J mapped=%J\n", relay_addr,
            mapped_addr);

    if (icem_set_turn_client(s->icem, ICE_COMP_ID, s->turnc) != 0) {
        ICE_LOG("libre_ice: icem_set_turn_client failed\n");
        s->gather_failed = true;
        turn_fail_fallback(s);
        return;
    }

    if (icem_lcand_add_base(s->icem, ICE_CAND_TYPE_RELAY, ICE_COMP_ID, 0,
                            "turn", ICE_TRANSP_UDP, relay_addr) != 0) {
        ICE_LOG("libre_ice: add relay candidate failed\n");
        s->gather_failed = true;
        turn_fail_fallback(s);
        return;
    }

    /* The TURN server's XOR-MAPPED-ADDRESS is the client's server-reflexive
     * address for this socket. Use it as the srflx candidate instead of a
     * separate STUN Binding request, which can collide with the TURN helper
     * when STUN and TURN share an address. */
    if (mapped_addr) {
        struct ice_cand *base =
            icem_cand_find(icem_lcandl(s->icem), ICE_COMP_ID, NULL);

        if (base && icem_lcand_add(s->icem, icem_lcand_base(base),
                                   ICE_CAND_TYPE_SRFLX,
                                   mapped_addr) != 0)
            ICE_LOG("libre_ice: add srflx from TURN mapped address failed\n");
    }

    emit_new_local_candidates(s);

    if (s->gather_pending > 0)
        s->gather_pending--;
    gather_maybe_done(s);
}

static int start_turn_gather(struct pj_ice_session *s)
{
    int err;

    /* Layer 0 is below the ICE helper (layer 1): incoming TURN ChannelData is
     * unwrapped before the ICE helper sees it, and outgoing packets pass
     * through the ICE helper first and are then routed by the TURN helper. */
    ICE_LOG("libre_ice: TURN gather %J\n", &s->turn_srv);
    err = turnc_alloc(&s->turnc, NULL, IPPROTO_UDP, s->us, 0, &s->turn_srv,
                      s->turn_user, s->turn_pass, ICE_TURN_LIFETIME,
                      turnc_handler, s);
    if (err) {
        ICE_LOG("libre_ice: turnc_alloc failed: %d\n", err);
        return err;
    }
    return 0;
}

/* ------------------------------------------------------------------------- */
/* connectivity checks                                                       */

static void connchk_handler(int err, bool update, void *arg)
{
    struct pj_ice_session *s = arg;
    const struct ice_cand *lc;
    const struct ice_cand *rc;

    (void)update;

    if (s->nego_done)
        return;

    if (err) {
        ICE_LOG("libre_ice: conncheck failed: %d\n", err);
        s->nego_done = true;
        s->nego_success = false;
        if (s->cfg.cb.ice_on_ice_complete)
            s->cfg.cb.ice_on_ice_complete((pj_ice_strans *)s,
                                          PJ_ICE_STRANS_OP_NEGOTIATION, err);
        return;
    }

    lc = icem_selected_lcand(s->icem, ICE_COMP_ID);
    rc = icem_selected_rcand(s->icem, ICE_COMP_ID);
    if (!lc || !rc) {
        ICE_LOG("libre_ice: conncheck ok without selected pair (lc=%p rc=%p)\n",
                (const void *)lc, (const void *)rc);
        return;
    }

    {
        int e1 = cand_from_libre(lc, &s->valid_lcand, s->valid_lfoundation,
                                 sizeof(s->valid_lfoundation));

        cand_from_libre_remote(rc, &s->valid_rcand, s->valid_rfoundation,
                               sizeof(s->valid_rfoundation));

        if (e1 != 0) {
            ICE_LOG("libre_ice: failed to encode local selected pair (e1=%d "
                    "lc=%J rc=%J)\n",
                    e1, icem_lcand_addr(lc), icem_lcand_addr(rc));
            s->nego_done = true;
            s->nego_success = false;
            if (s->cfg.cb.ice_on_ice_complete)
                s->cfg.cb.ice_on_ice_complete((pj_ice_strans *)s,
                                              PJ_ICE_STRANS_OP_NEGOTIATION,
                                              EPROTO);
            return;
        }
    }

    ICE_LOG("libre_ice: selected pair lcand=%J rcand=%J\n",
            icem_lcand_addr(lc), icem_lcand_addr(rc));
    s->valid_check.lcand = &s->valid_lcand;
    s->valid_check.rcand = &s->valid_rcand;
    s->has_valid_pair = true;
    s->nego_done = true;
    s->nego_success = true;

    if (s->cfg.cb.ice_on_ice_complete)
        s->cfg.cb.ice_on_ice_complete((pj_ice_strans *)s,
                                      PJ_ICE_STRANS_OP_NEGOTIATION, PJ_SUCCESS);
}

/* Apply queued remote credentials/candidates. Called on rtc_wrk only.
 *
 * Candidates are drained one at a time into a single 256-byte slot instead of
 * copying the whole 16x256 queue onto the stack: this is the deepest stack
 * user on rtc_wrk, whose measured peak is already ~6 KB. */
static void apply_pending_remote(struct pj_ice_session *s)
{
    char cand[256];
    char rufrag[sizeof(s->rufrag)];
    char rpwd[sizeof(s->rpwd)];
    bool have_ufrag = false;
    bool have_pwd = false;
    bool any = false;
    unsigned cand_cnt = 0;
    unsigned drained = 0;

    /* Do not consume the queued credentials/candidates before icem exists:
     * the flags are one-shot, so an early call during setup would discard the
     * remote ufrag/pwd and every later connectivity check would fail auth.
     * icem is read outside the lock: the session is destroyed only after the
     * worker is joined, so this thread cannot observe it changing. */
    if (!s->icem) {
        if ((s->pending_cand_cnt > 0 || s->has_rufrag || s->has_rpwd) &&
            (s->pending_cand_cnt != s->wait_log_pending || s->has_rufrag != s->wait_log_ufrag ||
             s->has_rpwd != s->wait_log_pwd)) {
            s->wait_log_pending = s->pending_cand_cnt;
            s->wait_log_ufrag = s->has_rufrag;
            s->wait_log_pwd = s->has_rpwd;
            ICE_LOG("libre_ice: pending remote waits for icem (cands=%u ufrag=%d pwd=%d)\n", s->pending_cand_cnt,
                    s->has_rufrag, s->has_rpwd);
        }
        return;
    }

    core_mutex_lock(s->lock);
    if (s->has_rufrag) {
        memcpy(rufrag, s->rufrag, sizeof(rufrag));
        s->has_rufrag = false;
        have_ufrag = true;
        any = true;
    }
    if (s->has_rpwd) {
        memcpy(rpwd, s->rpwd, sizeof(rpwd));
        s->has_rpwd = false;
        have_pwd = true;
        any = true;
    }
    core_mutex_unlock(s->lock);

    if (have_ufrag)
        (void)ice_sdp_decode(s->icem, "ice-ufrag", rufrag);
    if (have_pwd)
        (void)ice_sdp_decode(s->icem, "ice-pwd", rpwd);
    if (s->remote_ufrag_set && s->remote_pwd_set)
        s->remote_ready = true;

    /* Bound the drain so a burst of new candidates cannot keep this call
     * spinning; anything left is applied by the next signaling pass. */
    while (drained < ICE_MAX_PENDING_CANDS) {
        bool have_cand = false;

        core_mutex_lock(s->lock);
        if (s->pending_cand_cnt > 0) {
            memcpy(cand, s->pending_cands[0], sizeof(cand));
            s->pending_cand_cnt--;
            if (s->pending_cand_cnt > 0) {
                memmove(&s->pending_cands[0], &s->pending_cands[1],
                        sizeof(s->pending_cands[0]) * s->pending_cand_cnt);
            }
            have_cand = true;
        }
        core_mutex_unlock(s->lock);

        if (!have_cand)
            break;

        drained++;
        cand_cnt++;
        any = true;
        {
            int derr = icem_sdp_decode(s->icem, "candidate", cand);
            if (derr) {
                ICE_LOG("libre_ice: remote candidate decode failed err=%d\n", derr);
            }
        }

        /* Create a TURN permission as soon as a remote candidate is known.
         * ChannelBind also installs one, but the first connectivity check can
         * be sent before the (asynchronous) ChannelBind completes; a Send
         * Indication without a permission is dropped by the server. */
        if (s->turn_done && s->turnc) {
            struct ice_cand_attr attr;

            if (ice_cand_attr_decode(&attr, cand) == 0)
                (void)turnc_add_perm(s->turnc, &attr.addr, NULL, NULL);
        }
    }

    if (!any)
        return;

    if (drained > 0 || cand_cnt > 0) {
        ICE_LOG("libre_ice: pending remote applied: drained=%u cands=%u ready=%d gather=%d start=%d conncheck=%d\n",
                drained, cand_cnt, s->remote_ready, s->gather_done, s->start_requested, s->conncheck_started);
    }

    if (!s->nego_done && s->conncheck_started && cand_cnt)
        (void)icem_conncheck_start(s->icem);

    start_conncheck(s);
}

static void start_conncheck(struct pj_ice_session *s)
{
    int err;

    if (s->conncheck_started || !s->gather_done || !s->remote_ready ||
        !s->start_requested || !s->icem)
        return;

    err = icem_conncheck_start(s->icem);
    if (err) {
        ICE_LOG("libre_ice: conncheck start failed: %d\n", err);
        return;
    }
    s->conncheck_started = true;
}

/* ------------------------------------------------------------------------- */
/* setup / event loop                                                        */

static void udp_recv_handler(const struct sa *src, struct mbuf *mb, void *arg)
{
    struct pj_ice_session *s = arg;
    pj_sockaddr srcaddr;

    memset(&srcaddr, 0, sizeof(srcaddr));
    if (src && src->len <= sizeof(srcaddr))
        memcpy(&srcaddr, &src->u.sa, src->len);

    if (s->cfg.cb.ice_on_rx_data) {
        s->cfg.cb.ice_on_rx_data((pj_ice_strans *)s, ICE_COMP_ID,
                                 mb->buf + mb->pos, mb->end - mb->pos,
                                 &srcaddr, (unsigned)src->len);
    }
}

static int session_setup(struct pj_ice_session *s)
{
    struct sa any;
    int ice_layer = s->has_turn ? 1 : 0;
    int err;

    if (s->setup_done)
        return 0;

    s->role = (tolower((unsigned char)s->cfg.rolechar) == 'o')
                  ? ICE_ROLE_CONTROLLING
                  : ICE_ROLE_CONTROLLED;
    s->tiebrk = rand_u64() | 1;

    err = sa_set_str(&any, "0.0.0.0", 0);
    if (err)
        goto fail;

    err = udp_listen(&s->us, &any, udp_recv_handler, s);
    if (err)
        goto fail;

    udp_rxsz_set(s->us, ICE_UDP_RXSZ);

    err = udp_local_get(s->us, &s->laddr);
    if (err)
        goto fail;

    /* Binding to 0.0.0.0 gives no usable host candidate address: advertise
     * the interface address selected by the default route instead, keeping
     * the socket's ephemeral port. The socket still receives on all
     * interfaces. */
    {
        struct sa ifaddr;
        uint16_t port = sa_port(&s->laddr);

        if (net_default_source_addr_get(AF_INET, &ifaddr) == 0 &&
            sa_isset(&ifaddr, SA_ADDR)) {
            sa_cpy(&s->laddr, &ifaddr);
        } else {
            (void)sa_set_str(&s->laddr, "127.0.0.1", 0);
        }
        sa_set_port(&s->laddr, port);
    }

    err = icem_alloc(&s->icem, s->role, IPPROTO_UDP, ice_layer, s->tiebrk,
                     s->local_ufrag, s->local_passwd, connchk_handler, s);
    if (err) {
        ICE_LOG("libre_ice: icem_alloc failed: %d\n", err);
        goto fail;
    }

    if (s->cfg.relay_only)
        icem_conf(s->icem)->policy = ICE_POLICY_RELAY;

    icem_set_name(s->icem, "tuya_p2p");

    err = icem_comp_add(s->icem, ICE_COMP_ID, s->us);
    if (err) {
        ICE_LOG("libre_ice: icem_comp_add failed: %d\n", err);
        goto fail;
    }

    err = icem_lcand_add_base(s->icem, ICE_CAND_TYPE_HOST, ICE_COMP_ID, 0,
                              "tuya", ICE_TRANSP_UDP, &s->laddr);
    if (err) {
        ICE_LOG("libre_ice: add host candidate failed: %d\n", err);
        goto fail;
    }

    s->setup_done = true;
    emit_new_local_candidates(s);

    s->gather_pending = 0;
    s->stun_fallback = s->has_turn && s->has_stun;
    s->stun_started = false;

    if (s->has_turn) {
        s->gather_pending++;
        if (start_turn_gather(s) != 0) {
            s->has_turn = false;
            s->turn_done = true;
            turn_fail_fallback(s);
        }
    }

    /* If TURN is configured, STUN is only started as a fallback after TURN
     * fails: the TURN allocation's mapped address provides the srflx
     * candidate, and a concurrent STUN Binding response from the same server
     * address would be consumed by the TURN client. */
    if (!s->has_turn && s->has_stun && !s->stun_started) {
        s->stun_fallback = false;
        s->gather_pending++;
        if (start_stun_gather(s) != 0) {
            s->gather_pending--;
            s->gather_failed = true;
        }
    }

    if (s->gather_pending == 0)
        gather_maybe_done(s);

    apply_pending_remote(s);
    return 0;

 fail:
    s->setup_done = true;
    s->gather_done = true;
    s->gather_failed = true;
    s->nego_done = true;
    s->nego_success = false;
    if (s->cfg.cb.ice_on_ice_complete)
        s->cfg.cb.ice_on_ice_complete((pj_ice_strans *)s,
                                      PJ_ICE_STRANS_OP_INIT, err);
    return err;
}

static void flush_send_queue(struct pj_ice_session *s)
{
    struct ice_send_item *item;
    const struct ice_cand *rc;
    const struct sa *dst;

    core_mutex_lock(s->lock);
    item = s->send_head;
    s->send_head = s->send_tail = NULL;
    core_mutex_unlock(s->lock);

    rc = s->icem ? icem_selected_rcand(s->icem, ICE_COMP_ID) : NULL;
    dst = rc ? icem_lcand_addr(rc) : NULL;

    while (item) {
        struct ice_send_item *next = item->next;

        if (dst) {
            /* Reserve CHAN_HDR_SIZE bytes of headroom: libre's TURN helper
             * prepends the 4-byte ChannelData header in place and refuses to
             * wrap a packet whose mb->pos is smaller. */
            struct mbuf *mb = mbuf_alloc(item->len + 4);
            if (mb) {
                mb->pos = 4;
                if (mbuf_write_mem(mb, item->data, item->len) == 0) {
                    mb->pos = 4;
                    (void)udp_send(s->us, dst, mb);
                }
            }
            mem_deref(mb);
        }

        free(item);
        item = next;
    }
}

/* ------------------------------------------------------------------------- */
/* public API                                                               */

bool pj_thread_register2(void)
{
    /* libre does not require per-thread registration; the re context is
     * created by libre_init() on the signaling thread and shared read-only. */
    return true;
}

bool pj_ice_session_create(pj_ice_session_cfg_t *pCfg,
                           pj_ice_session_t **ppIceSession)
{
    struct pj_ice_session *s;
    int err;

    if (!pCfg || !ppIceSession || !pCfg->local_ufrag || !pCfg->local_passwd)
        return false;

    err = global_init();
    if (err) {
        ICE_LOG("libre_ice: libre_init failed: %d\n", err);
        return false;
    }

    s = calloc(1, sizeof(*s));
    if (!s)
        return false;

    s->cfg = *pCfg;
    str_ncpy(s->local_ufrag, pCfg->local_ufrag, sizeof(s->local_ufrag));
    str_ncpy(s->local_passwd, pCfg->local_passwd, sizeof(s->local_passwd));
    s->cfg.local_ufrag = s->local_ufrag;
    s->cfg.local_passwd = s->local_passwd;

    if (core_mutex_create_init(&s->lock) != OPRT_OK) {
        free(s);
        global_deinit();
        return false;
    }

    tmr_init(&s->pump_tmr);

    g_libre_users++;
    *ppIceSession = s;
    return true;
}

bool pj_ice_session_destroy(pj_ice_session_t *pIceSession)
{
    struct ice_send_item *item;

    if (!pIceSession)
        return false;

    tmr_cancel(&pIceSession->pump_tmr);

    if (pIceSession->icem)
        mem_deref(pIceSession->icem);
    if (pIceSession->turnc)
        mem_deref(pIceSession->turnc);
    if (pIceSession->us)
        mem_deref(pIceSession->us);

    core_mutex_lock(pIceSession->lock);
    item = pIceSession->send_head;
    pIceSession->send_head = pIceSession->send_tail = NULL;
    core_mutex_unlock(pIceSession->lock);
    while (item) {
        struct ice_send_item *next = item->next;
        free(item);
        item = next;
    }

    core_mutex_release(pIceSession->lock);
    free(pIceSession);

    if (g_libre_users > 0)
        g_libre_users--;
    global_deinit();
    return true;
}

bool pj_ice_session_init(pj_ice_session_t *pIceSession,
                         pj_ice_session_cfg_t *pCfg)
{
    cJSON *root;
    cJSON *item;

    if (!pIceSession || !pCfg || pIceSession->init_called)
        return pIceSession != NULL;

    pIceSession->init_called = true;

    root = cJSON_Parse(pCfg->server_tokens);
    if (!cJSON_IsArray(root)) {
        cJSON_Delete(root);
        return false;
    }

    cJSON_ArrayForEach(item, root) {
        cJSON *user;
        cJSON *cred;
        cJSON *urls;
        const char *url;
        char host[128];
        uint16_t port = 0;
        bool is_turn;
        int err;

        if (!cJSON_IsObject(item))
            continue;

        user = cJSON_GetObjectItemCaseSensitive(item, "username");
        cred = cJSON_GetObjectItemCaseSensitive(item, "credential");
        urls = cJSON_GetObjectItemCaseSensitive(item, "urls");
        if (!cJSON_IsString(urls))
            continue;

        url = urls->valuestring;
        if (strstr(url, "?transport=tcp") || strstr(url, "?transport=TCP"))
            continue;

        is_turn = strncmp(url, "turn:", 5) == 0;
        if (!is_turn && strncmp(url, "stun:", 5) != 0)
            continue;

        err = ice_parse_server_url(url, is_turn ? "turn:" : "stun:", host,
                                   sizeof(host), &port);
        if (err)
            continue;

        {
            struct sa srv;

            if (sa_set_str(&srv, host, port) != 0) {
                ICE_LOG("libre_ice: skip %s %s:%u (not an IP literal)\n",
                        is_turn ? "TURN" : "STUN", host, (unsigned)port);
                continue;
            }
            /* The ICE socket is IPv4-only (the app also drops IPv6 remote
             * candidates on the T5 LAN path); skip IPv6 servers instead of
             * failing later with EAFNOSUPPORT. */
            if (sa_af(&srv) != AF_INET) {
                ICE_LOG("libre_ice: skip %s %J (non-IPv4)\n",
                        is_turn ? "TURN" : "STUN", &srv);
                continue;
            }

            if (is_turn) {
                if (!cJSON_IsString(user) || !cJSON_IsString(cred))
                    continue;
                if (pIceSession->has_turn)
                    continue;
                sa_cpy(&pIceSession->turn_srv, &srv);
                str_ncpy(pIceSession->turn_user, user->valuestring,
                         sizeof(pIceSession->turn_user));
                str_ncpy(pIceSession->turn_pass, cred->valuestring,
                         sizeof(pIceSession->turn_pass));
                pIceSession->has_turn = true;
                ICE_LOG("libre_ice: TURN server %J\n", &srv);
            } else {
                if (pIceSession->has_stun)
                    continue;
                sa_cpy(&pIceSession->stun_srv, &srv);
                pIceSession->has_stun = true;
                ICE_LOG("libre_ice: STUN server %J\n", &srv);
            }
        }
    }

    cJSON_Delete(root);

#if !defined(P2P_ICE_ALLOW_DIRECT) || (P2P_ICE_ALLOW_DIRECT == 0)
    /* When the cloud provides a TURN server, prefer the relay<->relay path:
     * in a NATed call the peer's checks often only reach our relay candidate
     * (its own srflx port may differ from the signalled one), while a
     * host/srflx pair can be valid on one side only. Relay-only keeps both
     * sides on the TURN path that is reachable end-to-end. If TURN allocation
     * fails, the adapter falls back to STUN/host candidates.
     *
     * A build can set P2P_ICE_ALLOW_DIRECT=1 to keep the host/srflx
     * candidates alongside the relay so a direct pair can win when the peer's
     * network allows it; TURN remains available as the fallback. */
    if (pIceSession->has_turn)
        pIceSession->cfg.relay_only = true;
#endif

    /* Create the socket/icem and start gathering on the signalling thread,
     * matching pjproject's pj_ice_strans_init_ice() callback context. The
     * event loop itself is pumped later from rtc_wrk via re_main() (libre's
     * global re context is shared; see pj_ice_session_handle_events). */
    return session_setup(pIceSession) == 0;
}

bool pj_ice_session_add_remote_candidate(pj_ice_session_t *pIceSession,
                                         pj_str_t *rem_ufrag,
                                         pj_str_t *rem_passwd,
                                         unsigned rcand_cnt,
                                         pj_ice_sess_cand rcand[],
                                         pj_bool_t rcand_end)
{
    unsigned i;
    bool added = false;

    (void)rcand_end;

    if (!pIceSession)
        return false;

    core_mutex_lock(pIceSession->lock);

    if (rem_ufrag && rem_ufrag->ptr && rem_ufrag->slen) {
        size_t n = rem_ufrag->slen;
        if (n >= sizeof(pIceSession->rufrag))
            n = sizeof(pIceSession->rufrag) - 1;
        memcpy(pIceSession->rufrag, rem_ufrag->ptr, n);
        pIceSession->rufrag[n] = '\0';
        pIceSession->has_rufrag = true;
        pIceSession->remote_ufrag_set = true;
    }
    if (rem_passwd && rem_passwd->ptr && rem_passwd->slen) {
        size_t n = rem_passwd->slen;
        if (n >= sizeof(pIceSession->rpwd))
            n = sizeof(pIceSession->rpwd) - 1;
        memcpy(pIceSession->rpwd, rem_passwd->ptr, n);
        pIceSession->rpwd[n] = '\0';
        pIceSession->has_rpwd = true;
        pIceSession->remote_pwd_set = true;
    }

    for (i = 0; i < rcand_cnt; ++i) {
        char line[256];
        const char *value;
        int n;

        /* Relay-only means the peer can only reach us (and we can only reach
         * it) through TURN. A remote host candidate on a private/VPN address
         * has the highest ICE priority but is unreachable from the relay;
         * checking it first can consume the peer's entire connection timeout.
         * Drop such candidates so the srflx/relay pairs are checked first. */
        if (pIceSession->cfg.relay_only && rcand[i].type == PJ_ICE_CAND_TYPE_HOST &&
            rcand[i].addr.addr.sa_family == AF_INET) {
            const uint8_t *ip = (const uint8_t *)&rcand[i].addr.ipv4.sin_addr.s_addr;

            if (ip[0] == 10 || (ip[0] == 172 && (ip[1] & 0xF0) == 16) ||
                (ip[0] == 192 && ip[1] == 168) ||
                (ip[0] == 100 && ip[1] >= 64 && ip[1] <= 127) ||
                (ip[0] == 192 && ip[1] == 0 && ip[2] == 0) ||
                (ip[0] == 169 && ip[1] == 254) || ip[0] == 127 || ip[0] == 0 ||
                (ip[0] & 0xF0) == 224) {
                ICE_LOG("libre_ice: skip private host candidate "
                        "%u.%u.%u.%u (relay-only)\n",
                        ip[0], ip[1], ip[2], ip[3]);
                continue;
            }
        }

        if (pIceSession->pending_cand_cnt >= ICE_MAX_PENDING_CANDS)
            break;

        n = print_cand(line, sizeof(line), &rcand[i]);
        if (n < 0)
            continue;
        while (n > 0 && (line[n - 1] == '\r' || line[n - 1] == '\n'))
            line[--n] = '\0';

        /* libre's icem_sdp_decode() takes the attribute value, not the full
         * "a=candidate:" line. */
        value = line;
        if (strncmp(value, "a=candidate:", 12) == 0)
            value += 12;

        str_ncpy(pIceSession->pending_cands[pIceSession->pending_cand_cnt],
                 value, sizeof(pIceSession->pending_cands[0]));
        pIceSession->pending_cand_cnt++;
        added = true;
    }

    core_mutex_unlock(pIceSession->lock);

    ICE_LOG("libre_ice: remote cand add: queued=%d pending=%u icem=%p start=%d rufrag=%d rpwd=%d\n", added,
            pIceSession->pending_cand_cnt, (void *)pIceSession->icem, pIceSession->start_requested,
            pIceSession->remote_ufrag_set, pIceSession->remote_pwd_set);

    /* Do not ask to start connectivity checks when every offered candidate
     * was filtered (e.g. private hosts under relay-only): forming the
     * checklist with zero remote candidates fails and would leave the
     * session in a started-but-empty state. */
    if (pIceSession->remote_ufrag_set && pIceSession->remote_pwd_set &&
        (added || pIceSession->pending_cand_cnt > 0 ||
         (pIceSession->icem && list_count(icem_rcandl(pIceSession->icem)) > 0)))
        pIceSession->start_requested = true;

    return true;
}

bool pj_ice_session_sendto(pj_ice_session_t *pIceSession, void *pkt,
                           uint32_t len)
{
    struct ice_send_item *item;

    if (!pIceSession || !pkt || !len || !pIceSession->has_valid_pair)
        return false;

    item = malloc(sizeof(*item) + len);
    if (!item)
        return false;

    item->next = NULL;
    item->len = len;
    memcpy(item->data, pkt, len);

    core_mutex_lock(pIceSession->lock);
    if (pIceSession->send_tail)
        pIceSession->send_tail->next = item;
    else
        pIceSession->send_head = item;
    pIceSession->send_tail = item;
    core_mutex_unlock(pIceSession->lock);
    return true;
}

bool pj_ice_session_handle_events(pj_ice_session_t *pIceSession,
                                  unsigned max_msec, unsigned *p_count)
{
    int err;

    if (!pIceSession)
        return false;

    if (!pIceSession->setup_done) {
        /* pj_ice_session_init() calls session_setup(); this only happens if
         * the caller skipped init or setup failed. */
        if (p_count)
            *p_count = 0;
        return false;
    }

    apply_pending_remote(pIceSession);
    flush_send_queue(pIceSession);

    if (max_msec == 0)
        max_msec = 1;

    tmr_start(&pIceSession->pump_tmr, max_msec, pump_cancel, NULL);
    err = re_main(NULL);
    if (err) {
        ICE_LOG("libre_ice: re_main failed: %d\n", err);
        if (p_count)
            *p_count = 0;
        return false;
    }

    if (p_count)
        *p_count = 0;
    return true;
}

void pj_ice_session_dbg_dump(pj_ice_session_t *pIceSession, const char *tag)
{
    if (!pIceSession)
        return;

    ICE_LOG("libre_ice[%s]: lcand=%u rcand=%u conncheck=%d nego=%d/%d\n",
            tag ? tag : "?", pIceSession->lcand_cnt,
            pIceSession->icem ? list_count(icem_rcandl(pIceSession->icem)) : 0,
            pIceSession->conncheck_started, pIceSession->nego_done,
            pIceSession->nego_success);
}

bool pj_ice_session_try_start_ice(pj_ice_session_t *pIceSession, const char *tag);

bool pj_ice_session_on_local_gather_done(pj_ice_session_t *pIceSession)
{
    if (!pIceSession)
        return false;

    pIceSession->local_gather_done = true;
    /* Only request conncheck once remote credentials/candidates exist;
     * starting with an empty checklist fails and spams "conncheck start
     * failed" (icem_conncheck_start -> 2). add_remote_candidate() and the
     * signalling path call try_start_ice() again when they arrive. */
    (void)pj_ice_session_try_start_ice(pIceSession, "local_gather_done");
    return true;
}

bool pj_ice_session_try_start_ice(pj_ice_session_t *pIceSession,
                                  const char *tag)
{
    (void)tag;

    if (!pIceSession)
        return false;

    if (pIceSession->local_gather_done && pIceSession->remote_ufrag_set &&
        pIceSession->remote_pwd_set &&
        (pIceSession->pending_cand_cnt > 0 ||
         (pIceSession->icem && list_count(icem_rcandl(pIceSession->icem)) > 0)))
        pIceSession->start_requested = true;

    return true;
}

bool pj_ice_session_is_nego_done(pj_ice_session_t *pIceSession)
{
    return pIceSession && pIceSession->nego_done;
}

bool pj_ice_session_is_nego_success(pj_ice_session_t *pIceSession)
{
    return pIceSession && pIceSession->nego_success;
}

/* pj_ice_strans_* shims used by tuya_media_service_rtc.c */

void *pj_ice_strans_get_user_data(pj_ice_strans *ice_st)
{
    return ice_st ? ice_st->cfg.user_data : NULL;
}

const pj_ice_sess_check *pj_ice_strans_get_valid_pair(pj_ice_strans *ice_st,
                                                      unsigned comp_id)
{
    (void)comp_id;

    if (!ice_st || !ice_st->has_valid_pair)
        return NULL;
    return &ice_st->valid_check;
}

pj_status_t pj_ice_strans_enum_cands(pj_ice_strans *ice_st, unsigned comp_id,
                                     unsigned *count,
                                     pj_ice_sess_cand cands[])
{
    unsigned n;
    unsigned i;

    (void)comp_id;

    if (!ice_st || !count || !cands)
        return EINVAL;

    n = *count;
    if (n > PJ_ICE_MAX_CAND)
        n = PJ_ICE_MAX_CAND;

    /* enum_cands is called from the on_new_candidate callback, i.e. on the
     * thread that owns the icem object. Rebuild the snapshot so it reflects
     * redund_elim/default-candidate processing. */
    if (ice_st->icem)
        cache_rebuild(ice_st);

    if (ice_st->lcand_cnt < n)
        n = ice_st->lcand_cnt;

    for (i = 0; i < n; ++i)
        cands[i] = ice_st->lcands[i];

    *count = n;
    return PJ_SUCCESS;
}

pj_bool_t pj_ice_strans_has_sess(pj_ice_strans *ice_st)
{
    return ice_st && ice_st->setup_done ? PJ_TRUE : PJ_FALSE;
}

pj_bool_t pj_ice_strans_sess_is_complete(pj_ice_strans *ice_st)
{
    return ice_st && ice_st->nego_done && ice_st->nego_success ? PJ_TRUE
                                                               : PJ_FALSE;
}

pj_ice_strans_state pj_ice_strans_get_state(pj_ice_strans *ice_st)
{
    if (!ice_st)
        return PJ_ICE_STRANS_STATE_NULL;
    if (ice_st->nego_success)
        return PJ_ICE_STRANS_STATE_RUNNING;
    if (ice_st->nego_done)
        return PJ_ICE_STRANS_STATE_FAILED;
    if (ice_st->conncheck_started)
        return PJ_ICE_STRANS_STATE_NEGO_READY;
    return PJ_ICE_STRANS_STATE_SESS_READY;
}
