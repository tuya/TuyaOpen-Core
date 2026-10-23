#include <bluetooth/bluetooth.h>
#include <bluetooth/hci.h>
#include <bluetooth/hci_lib.h>

#include "tuya_cloud_types.h"   /* this layer's common types (uint8_t/bool etc.) */
#include "tuya_error_code.h"    /* this layer's error codes (OPRT_*) */

/* Logging goes through printf only; this layer's headers provide the OPRT_*
 * error codes and uint8_t, so no core header is included. */

int tuya_hci_le_set_adv_params(uint16_t min_interval, uint16_t max_interval, uint8_t  advtype)
{
    int device = 0;
    uint8_t status = 0;

    // min_interval = 32;
    // max_interval = 32;
    advtype = 1;
    printf("===== min_interval = %d, max_interval = %d, advtype = %d\r\n", min_interval, max_interval, advtype);
    device = hci_open_dev(hci_get_route(NULL));
    if (device < 0) {
        return OPRT_COM_ERROR;
    }

    le_set_advertising_parameters_cp adv_params_cp;
    memset(&adv_params_cp, 0, sizeof(adv_params_cp));
    adv_params_cp.advtype = advtype;
    adv_params_cp.min_interval = htobs(min_interval);
    adv_params_cp.max_interval = htobs(max_interval);
    adv_params_cp.chan_map = 7;

    struct hci_request req;
    memset(&req, 0, sizeof(req));
    req.ogf = OGF_LE_CTL;
    req.ocf = OCF_LE_SET_ADVERTISING_PARAMETERS;
    req.cparam = &adv_params_cp;
    req.clen = LE_SET_ADVERTISING_PARAMETERS_CP_SIZE;
    req.rparam = &status;
    req.rlen = 1;

    if (hci_send_req(device, &req, 1000) != 0) {
        hci_close_dev(device);
        return OPRT_RECV_ERR;
    }

    printf("Set Advertisement Parameters, status 0x%02x\n", status);

    hci_close_dev(device);

    return OPRT_OK;
}

int tuya_hci_le_set_adv_enable(bool enable)
{
    int device = 0;
    uint8_t status = 0;

    device = hci_open_dev(hci_get_route(NULL));
    if (device < 0) {
        return OPRT_COM_ERROR;
    }

    le_set_advertise_enable_cp advertise_cp;
    memset(&advertise_cp, 0, sizeof(advertise_cp));
    advertise_cp.enable = enable;

    struct hci_request req;
    memset(&req, 0, sizeof(req));
    req.ogf = OGF_LE_CTL;
    req.ocf = OCF_LE_SET_ADVERTISE_ENABLE;
    req.cparam = &advertise_cp;
    req.clen = LE_SET_ADVERTISE_ENABLE_CP_SIZE;
    req.rparam = &status;
    req.rlen = 1;

    if (hci_send_req(device, &req, 1000) != 0) {
        hci_close_dev(device);
        return OPRT_RECV_ERR;
    }

    printf("Set Advertisement %s, status 0x%02x\n", enable ? "Enable" : "Disable", status);

    hci_close_dev(device);

    return OPRT_OK;
}

int tuya_hci_le_set_adv_data(uint8_t *data, uint8_t len)
{
    int device = 0;
    uint8_t status = 0;

    if ((data == NULL) || (len == 0)) {
        return OPRT_INVALID_PARM;
    }

    device = hci_open_dev(hci_get_route(NULL));
    if (device < 0) {
        return OPRT_COM_ERROR;
    }

    le_set_advertising_data_cp adv_data_cp;
    memset(&adv_data_cp, 0, sizeof(adv_data_cp));
    memcpy(adv_data_cp.data, data, len);
    adv_data_cp.length = len;

    struct hci_request req;
    memset(&req, 0, sizeof(req));
    req.ogf = OGF_LE_CTL;
    req.ocf = OCF_LE_SET_ADVERTISING_DATA;
    req.cparam = &adv_data_cp;
    req.clen = LE_SET_ADVERTISING_DATA_CP_SIZE;
    req.rparam = &status;
    req.rlen = 1;

    if (hci_send_req(device, &req, 1000) != 0) {
        hci_close_dev(device);
        return OPRT_RECV_ERR;
    }

    printf("Set Advertisement Data, status 0x%02x\n", status);

    hci_close_dev(device);

    return OPRT_OK;
}

int tuya_hci_le_set_scan_rsp_data(uint8_t *data, uint8_t len)
{
    int device = 0;
    uint8_t status = 0;

    if ((data == NULL) || (len == 0)) {
        return OPRT_INVALID_PARM;
    }

    device = hci_open_dev(hci_get_route(NULL));
    if (device < 0) {
        return OPRT_COM_ERROR;
    }

    le_set_scan_response_data_cp scan_rsp_data_cp;
    memset(&scan_rsp_data_cp, 0, sizeof(scan_rsp_data_cp));
    memcpy(scan_rsp_data_cp.data, data, len);
    scan_rsp_data_cp.length = len;

    struct hci_request req;
    memset(&req, 0, sizeof(req));
    req.ogf = OGF_LE_CTL;
    req.ocf = OCF_LE_SET_SCAN_RESPONSE_DATA;
    req.cparam = &scan_rsp_data_cp;
    req.clen = LE_SET_SCAN_RESPONSE_DATA_CP_SIZE;
    req.rparam = &status;
    req.rlen = 1;

    if (hci_send_req(device, &req, 1000) != 0) {
        hci_close_dev(device);
        return OPRT_RECV_ERR;
    }

    printf("Set Scan Response Data, status 0x%02x\n", status);

    hci_close_dev(device);

    return OPRT_OK;
}

