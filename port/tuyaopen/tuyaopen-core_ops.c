/**
 * @file tuyaopen-core_ops.c
 * @brief Platform ops cards for tuyaopen-core on the legacy tuyaopen platforms
 *        (tuyaos / tuya_open_sdk): CORE_API_OPS_T, CORE_NETWORK_OPS_T,
 *        CORE_NETIF_OPS_T, CORE_BLUETOOTH_OPS_T and CORE_FS_OPS_T.
 *
 * @copyright Copyright (c) 2021-2026 Tuya Inc. All Rights Reserved.
 */

#include <stddef.h>

#include "tuya_kconfig.h"

#include "core_api_ops.h"
#include "core_network_ops.h"
#include "core_netif_ops.h"
#include "core_bluetooth_ops.h"
#include "core_fs_ops.h"

#include "tkl_output.h"
#include "tkl_memory.h"
#include "tkl_system.h"
#include "tkl_semaphore.h"
#include "tkl_mutex.h"
#include "tkl_thread.h"
#include "tkl_queue.h"
#include "tkl_network.h"
#if defined(ENABLE_BLUETOOTH) && (ENABLE_BLUETOOTH == 1) && defined(TUYAOPEN_HAS_TKL_BLE)
#include "tkl_bluetooth.h"
#endif
#include "tkl_fs.h"

#if defined(ESP_PLATFORM)
#include "netif_esp.h"
#else
#include "tkl_wired.h"
#endif

/* CORE_API_OPS_T: log / memory / system / semaphore / mutex / thread / queue */
CORE_API_OPS_T core_api_ops_platform = {
    //! log output
    .log_output = tkl_log_output,
    //! memory
    .malloc = tkl_system_malloc,
    .free   = tkl_system_free,
    //! system
    .sleep           = tkl_system_sleep,
    .reset           = tkl_system_reset,
    .get_millisecond = tkl_system_get_millisecond,
    .get_random      = tkl_system_get_random,
    //! semaphore
    .semaphore_create_init = tkl_semaphore_create_init,
    .semaphore_wait        = tkl_semaphore_wait,
    .semaphore_post        = tkl_semaphore_post,
    .semaphore_release     = tkl_semaphore_release,
    //! mutex
    .mutex_create_init = tkl_mutex_create_init,
    .mutex_lock        = tkl_mutex_lock,
    .mutex_unlock      = tkl_mutex_unlock,
    .mutex_release     = tkl_mutex_release,
    //! thread
    .thread_create        = tkl_thread_create,
    .thread_release       = tkl_thread_release,
    .thread_set_self_name = tkl_thread_set_self_name,
    .thread_is_self       = tkl_thread_is_self,
    .thread_get_id        = tkl_thread_get_id,
    //! queue
    .queue_create_init = tkl_queue_create_init,
    .queue_post        = tkl_queue_post,
    .queue_fetch       = tkl_queue_fetch,
    .queue_free        = tkl_queue_free,
};

/* CORE_NETWORK_OPS_T: socket API */
CORE_NETWORK_OPS_T core_network_ops_platform = {
    .get_errno = tkl_net_get_errno,
    .fd_set = tkl_net_fd_set,
    .fd_isset = tkl_net_fd_isset,
    .fd_zero = tkl_net_fd_zero,
    .select = tkl_net_select,
    .get_nonblock = tkl_net_get_nonblock,
    .set_block = tkl_net_set_block,
    .close = tkl_net_close,
    .socket_create = tkl_net_socket_create,
    .connect = tkl_net_connect,
    .bind = tkl_net_bind,
    .send = tkl_net_send,
    .recv = tkl_net_recv,
    .set_timeout = tkl_net_set_timeout,
    .set_reuse = tkl_net_set_reuse,
    .disable_nagle = tkl_net_disable_nagle,
    .gethostbyname = tkl_net_gethostbyname,
    .set_keepalive = tkl_net_set_keepalive,
    .str2addr = tkl_net_str2addr,
    .addr2str = tkl_net_addr2str,
    .setsockopt = tkl_net_setsockopt,
    //! local link / p2p members
    .listen = tkl_net_listen,
    .accept = tkl_net_accept,
    .send_to = tkl_net_send_to,
    .recvfrom = tkl_net_recvfrom,
    .recv_nd_size = tkl_net_recv_nd_size,
#if defined(ENABLE_TUYA_P2P)
    .fd_clear = tkl_net_fd_clear,
    .shutdown = tkl_net_shutdown,
    .set_bufsize = tkl_net_set_bufsize,
    .getsockname = tkl_net_getsockname,
    .getpeername = tkl_net_getpeername,
#endif
};

/* CORE_NETIF_OPS_T: link status and local IP */
#if defined(ESP_PLATFORM)
CORE_NETIF_OPS_T core_netif_ops_platform = {
    .get_ip      = core_netif_esp_get_ip,
    .set_link_cb = core_netif_esp_set_link_cb,
};
#else
static CORE_NETIF_LINK_CHANGE_CB s_link_cb;

static void __link_cb_shim(TKL_WIRED_STAT_E status)
{
    if (s_link_cb) {
        s_link_cb((CORE_NETIF_LINK_STAT_E)status);
    }
}

static OPERATE_RET __set_link_cb(CORE_NETIF_LINK_CHANGE_CB cb)
{
    s_link_cb = cb;
    return tkl_wired_set_status_cb(__link_cb_shim);
}

#if !defined(__linux__) || (defined(ENABLE_WIRED) && (ENABLE_WIRED == 1))
CORE_NETIF_OPS_T core_netif_ops_platform = {
    .get_ip      = tkl_wired_get_ip,
    .set_link_cb = __set_link_cb,
};
#endif
#endif

/* CORE_FS_OPS_T: file and directory API */
CORE_FS_OPS_T core_fs_ops_platform = {
    //! file
    .fopen   = tkl_fopen,
    .fclose  = tkl_fclose,
    .fwrite  = tkl_fwrite,
    .fread   = tkl_fread,
    .fseek   = tkl_fseek,
    .fflush  = tkl_fflush,
    .fsync   = tkl_fsync,
    .fileno  = tkl_fileno,
    .fgets   = tkl_fgets,
    //! path
    .fs_is_exist = tkl_fs_is_exist,
    .fs_mkdir    = tkl_fs_mkdir,
    .fs_remove   = tkl_fs_remove,
    .fs_rename   = tkl_fs_rename,
    //! directory
    .dir_open         = tkl_dir_open,
    .dir_close        = tkl_dir_close,
    .dir_read         = tkl_dir_read,
    .dir_name         = tkl_dir_name,
    .dir_is_directory = tkl_dir_is_directory,
};

/* CORE_BLUETOOTH_OPS_T: peripheral GAP / GATT server API */
#if defined(ENABLE_BLUETOOTH) && (ENABLE_BLUETOOTH == 1) && defined(TUYAOPEN_HAS_TKL_BLE)

/* sizeof checks between the core mirror types and the tkl types */
#define CORE_BLE_LAYOUT_ASSERT(mirror, tkl) \
    typedef char _core_ble_assert_##mirror[(sizeof(mirror) == sizeof(tkl)) ? 1 : -1]

CORE_BLE_LAYOUT_ASSERT(CORE_BLE_GAP_ADDR_T, TKL_BLE_GAP_ADDR_T);
CORE_BLE_LAYOUT_ASSERT(CORE_BLE_DATA_T, TKL_BLE_DATA_T);
CORE_BLE_LAYOUT_ASSERT(CORE_BLE_GAP_ADV_PARAMS_T, TKL_BLE_GAP_ADV_PARAMS_T);
CORE_BLE_LAYOUT_ASSERT(CORE_BLE_GAP_ADV_REPORT_T, TKL_BLE_GAP_ADV_REPORT_T);
CORE_BLE_LAYOUT_ASSERT(CORE_BLE_GAP_SCAN_PARAMS_T, TKL_BLE_GAP_SCAN_PARAMS_T);
CORE_BLE_LAYOUT_ASSERT(CORE_BLE_GAP_CONN_PARAMS_T, TKL_BLE_GAP_CONN_PARAMS_T);
CORE_BLE_LAYOUT_ASSERT(CORE_BLE_UUID_T, TKL_BLE_UUID_T);
CORE_BLE_LAYOUT_ASSERT(CORE_BLE_CHAR_PARAMS_T, TKL_BLE_CHAR_PARAMS_T);
CORE_BLE_LAYOUT_ASSERT(CORE_BLE_SERVICE_PARAMS_T, TKL_BLE_SERVICE_PARAMS_T);
CORE_BLE_LAYOUT_ASSERT(CORE_BLE_GATTS_PARAMS_T, TKL_BLE_GATTS_PARAMS_T);
CORE_BLE_LAYOUT_ASSERT(CORE_BLE_GAP_CONNECT_EVT_T, TKL_BLE_GAP_CONNECT_EVT_T);
CORE_BLE_LAYOUT_ASSERT(CORE_BLE_GAP_DISCONNECT_EVT_T, TKL_BLE_GAP_DISCONNECT_EVT_T);
CORE_BLE_LAYOUT_ASSERT(CORE_BLE_NOTIFY_RESULT_EVT_T, TKL_BLE_NOTIFY_RESULT_EVT_T);
CORE_BLE_LAYOUT_ASSERT(CORE_BLE_GATT_SVC_HANDLE_T, TKL_BLE_GATT_SVC_HANDLE_T);
CORE_BLE_LAYOUT_ASSERT(CORE_BLE_GATT_SVC_DISC_TYPE_T, TKL_BLE_GATT_SVC_DISC_TYPE_T);
CORE_BLE_LAYOUT_ASSERT(CORE_BLE_GATT_CHAR_HANDLE_T, TKL_BLE_GATT_CHAR_HANDLE_T);
CORE_BLE_LAYOUT_ASSERT(CORE_BLE_GATT_CHAR_DISC_TYPE_T, TKL_BLE_GATT_CHAR_DISC_TYPE_T);
CORE_BLE_LAYOUT_ASSERT(CORE_BLE_GATT_DESC_DISC_TYPE_T, TKL_BLE_GATT_DESC_DISC_TYPE_T);
CORE_BLE_LAYOUT_ASSERT(CORE_BLE_GATT_DATA_REPORT_T, TKL_BLE_DATA_REPORT_T);
CORE_BLE_LAYOUT_ASSERT(CORE_BLE_GATT_SUBSCRIBE_EVT_T, TKL_BLE_SUBSCRBE_EVT_T);
CORE_BLE_LAYOUT_ASSERT(CORE_BLE_GATT_READ_CHAR_EVT_T, TKL_BLE_READ_CHAR_EVT_T);
CORE_BLE_LAYOUT_ASSERT(CORE_BLE_GAP_PARAMS_EVT_T, TKL_BLE_GAP_PARAMS_EVT_T);
CORE_BLE_LAYOUT_ASSERT(CORE_BLE_GATT_PARAMS_EVT_T, TKL_BLE_GATT_PARAMS_EVT_T);

/* offsetof checks for the event fields read by core */
#define CORE_BLE_OFFSET_ASSERT(mirror, tkl, field) \
    typedef char _core_ble_off_assert_##mirror##_##field[(offsetof(mirror, field) == offsetof(tkl, field)) ? 1 : -1]

CORE_BLE_OFFSET_ASSERT(CORE_BLE_GAP_PARAMS_EVT_T, TKL_BLE_GAP_PARAMS_EVT_T, conn_handle);
CORE_BLE_OFFSET_ASSERT(CORE_BLE_GAP_PARAMS_EVT_T, TKL_BLE_GAP_PARAMS_EVT_T, result);
CORE_BLE_OFFSET_ASSERT(CORE_BLE_GAP_PARAMS_EVT_T, TKL_BLE_GAP_PARAMS_EVT_T, gap_event);
CORE_BLE_OFFSET_ASSERT(CORE_BLE_GATT_PARAMS_EVT_T, TKL_BLE_GATT_PARAMS_EVT_T, conn_handle);
CORE_BLE_OFFSET_ASSERT(CORE_BLE_GATT_PARAMS_EVT_T, TKL_BLE_GATT_PARAMS_EVT_T, result);
CORE_BLE_OFFSET_ASSERT(CORE_BLE_GATT_PARAMS_EVT_T, TKL_BLE_GATT_PARAMS_EVT_T, gatt_event);
CORE_BLE_OFFSET_ASSERT(CORE_BLE_GAP_ADV_REPORT_T, TKL_BLE_GAP_ADV_REPORT_T, adv_type);
CORE_BLE_OFFSET_ASSERT(CORE_BLE_GAP_ADV_REPORT_T, TKL_BLE_GAP_ADV_REPORT_T, peer_addr);
CORE_BLE_OFFSET_ASSERT(CORE_BLE_GAP_ADV_REPORT_T, TKL_BLE_GAP_ADV_REPORT_T, rssi);
CORE_BLE_OFFSET_ASSERT(CORE_BLE_GAP_ADV_REPORT_T, TKL_BLE_GAP_ADV_REPORT_T, data);
CORE_BLE_OFFSET_ASSERT(CORE_BLE_GAP_CONNECT_EVT_T, TKL_BLE_GAP_CONNECT_EVT_T, role);
CORE_BLE_OFFSET_ASSERT(CORE_BLE_GAP_CONNECT_EVT_T, TKL_BLE_GAP_CONNECT_EVT_T, peer_addr);
CORE_BLE_OFFSET_ASSERT(CORE_BLE_GAP_DISCONNECT_EVT_T, TKL_BLE_GAP_DISCONNECT_EVT_T, reason);
CORE_BLE_OFFSET_ASSERT(CORE_BLE_GAP_DISCONNECT_EVT_T, TKL_BLE_GAP_DISCONNECT_EVT_T, role);
CORE_BLE_OFFSET_ASSERT(CORE_BLE_GAP_CONN_PARAMS_T, TKL_BLE_GAP_CONN_PARAMS_T, conn_interval_min);
CORE_BLE_OFFSET_ASSERT(CORE_BLE_GAP_CONN_PARAMS_T, TKL_BLE_GAP_CONN_PARAMS_T, conn_interval_max);
CORE_BLE_OFFSET_ASSERT(CORE_BLE_GAP_CONN_PARAMS_T, TKL_BLE_GAP_CONN_PARAMS_T, conn_latency);
CORE_BLE_OFFSET_ASSERT(CORE_BLE_GAP_CONN_PARAMS_T, TKL_BLE_GAP_CONN_PARAMS_T, conn_sup_timeout);
CORE_BLE_OFFSET_ASSERT(CORE_BLE_NOTIFY_RESULT_EVT_T, TKL_BLE_NOTIFY_RESULT_EVT_T, char_handle);
CORE_BLE_OFFSET_ASSERT(CORE_BLE_NOTIFY_RESULT_EVT_T, TKL_BLE_NOTIFY_RESULT_EVT_T, result);
CORE_BLE_OFFSET_ASSERT(CORE_BLE_GATT_DATA_REPORT_T, TKL_BLE_DATA_REPORT_T, char_handle);
CORE_BLE_OFFSET_ASSERT(CORE_BLE_GATT_DATA_REPORT_T, TKL_BLE_DATA_REPORT_T, report);
CORE_BLE_OFFSET_ASSERT(CORE_BLE_GATT_SUBSCRIBE_EVT_T, TKL_BLE_SUBSCRBE_EVT_T, char_handle);
CORE_BLE_OFFSET_ASSERT(CORE_BLE_GATT_SUBSCRIBE_EVT_T, TKL_BLE_SUBSCRBE_EVT_T, reason);
CORE_BLE_OFFSET_ASSERT(CORE_BLE_GATT_READ_CHAR_EVT_T, TKL_BLE_READ_CHAR_EVT_T, char_handle);
CORE_BLE_OFFSET_ASSERT(CORE_BLE_GATT_READ_CHAR_EVT_T, TKL_BLE_READ_CHAR_EVT_T, offset);

/* callback wrappers */
static OPERATE_RET core_ble_ops_gap_callback_register(const CORE_BLE_GAP_EVT_FUNC_CB gap_evt)
{
    return tkl_ble_gap_callback_register((TKL_BLE_GAP_EVT_FUNC_CB)gap_evt);
}

static OPERATE_RET core_ble_ops_gatt_callback_register(const CORE_BLE_GATT_EVT_FUNC_CB gatt_evt)
{
    return tkl_ble_gatt_callback_register((TKL_BLE_GATT_EVT_FUNC_CB)gatt_evt);
}

/* struct-argument wrappers */
static OPERATE_RET core_ble_ops_gap_addr_set(CORE_BLE_GAP_ADDR_T const *p_peer_addr)
{
    return tkl_ble_gap_addr_set((TKL_BLE_GAP_ADDR_T const *)p_peer_addr);
}

static OPERATE_RET core_ble_ops_gap_address_get(CORE_BLE_GAP_ADDR_T *p_peer_addr)
{
    return tkl_ble_gap_address_get((TKL_BLE_GAP_ADDR_T *)p_peer_addr);
}

static OPERATE_RET core_ble_ops_gap_adv_start(CORE_BLE_GAP_ADV_PARAMS_T const *p_adv_params)
{
    return tkl_ble_gap_adv_start((TKL_BLE_GAP_ADV_PARAMS_T const *)p_adv_params);
}

static OPERATE_RET core_ble_ops_gap_adv_rsp_data_set(CORE_BLE_DATA_T const *p_adv, CORE_BLE_DATA_T const *p_scan_rsp)
{
    return tkl_ble_gap_adv_rsp_data_set((TKL_BLE_DATA_T const *)p_adv, (TKL_BLE_DATA_T const *)p_scan_rsp);
}

static OPERATE_RET core_ble_ops_gap_adv_rsp_data_update(CORE_BLE_DATA_T const *p_adv, CORE_BLE_DATA_T const *p_scan_rsp)
{
    return tkl_ble_gap_adv_rsp_data_update((TKL_BLE_DATA_T const *)p_adv, (TKL_BLE_DATA_T const *)p_scan_rsp);
}

static OPERATE_RET core_ble_ops_gap_conn_param_update(uint16_t conn_handle,
                                                      CORE_BLE_GAP_CONN_PARAMS_T const *p_conn_params)
{
    return tkl_ble_gap_conn_param_update(conn_handle, (TKL_BLE_GAP_CONN_PARAMS_T const *)p_conn_params);
}

/* non-const pointer: the tkl implementation writes service/char handles back */
static OPERATE_RET core_ble_ops_gatts_service_add(CORE_BLE_GATTS_PARAMS_T *p_service)
{
    return tkl_ble_gatts_service_add((TKL_BLE_GATTS_PARAMS_T *)p_service);
}

CORE_BLUETOOTH_OPS_T core_bluetooth_ops_platform = {
    //! stack
    .stack_init               = tkl_ble_stack_init,
    .stack_deinit             = tkl_ble_stack_deinit,
    .stack_gatt_link          = tkl_ble_stack_gatt_link,
    //! event registration
    .gap_callback_register    = core_ble_ops_gap_callback_register,
    .gatt_callback_register   = core_ble_ops_gatt_callback_register,
    //! GAP
    .gap_addr_set             = core_ble_ops_gap_addr_set,
    .gap_address_get          = core_ble_ops_gap_address_get,
    .gap_adv_start            = core_ble_ops_gap_adv_start,
    .gap_adv_stop             = tkl_ble_gap_adv_stop,
    .gap_adv_rsp_data_set     = core_ble_ops_gap_adv_rsp_data_set,
    .gap_adv_rsp_data_update  = core_ble_ops_gap_adv_rsp_data_update,
    .gap_disconnect           = tkl_ble_gap_disconnect,
    .gap_conn_param_update    = core_ble_ops_gap_conn_param_update,
    .gap_rssi_get             = tkl_ble_gap_rssi_get,
    //! GATT server
    .gatts_service_add        = core_ble_ops_gatts_service_add,
    .gatts_value_set          = tkl_ble_gatts_value_set,
    .gatts_value_notify       = tkl_ble_gatts_value_notify,
    .gatts_exchange_mtu_reply = tkl_ble_gatts_exchange_mtu_reply,
};

#else /* !defined(ENABLE_BLUETOOTH) || (ENABLE_BLUETOOTH == 0) */

CORE_BLUETOOTH_OPS_T core_bluetooth_ops_platform = { 0 };

#endif /* defined(ENABLE_BLUETOOTH) && (ENABLE_BLUETOOTH == 1) */
