#include <glib.h>
#include <stdio.h>
#include <signal.h>
#include <unistd.h>
#include "adapter.h"
#include "device.h"
#include "logger.h"
#include "agent.h"
#include "application.h"
#include "advertisement.h"
#include "utility.h"
#include "parser.h"

#include <pthread.h>
#include "port_bluetooth.h"

#define TAG "BT_DBUS_API"

static GMainLoop *loop = NULL;
static Adapter *default_adapter = NULL;
static Advertisement *advertisement = NULL;
static Application *app = NULL;
static Agent *agent = NULL;
static volatile sig_atomic_t cleanup_in_progress = 0;

static CORE_BLE_GAP_EVT_FUNC_CB  __gap_evt_cb  = NULL;
static CORE_BLE_GATT_EVT_FUNC_CB __gatt_evt_cb = NULL;

////////////////////////////////////////////////
// Manage the mapping between characteristics and 16-bit UUIDs
////////////////////////////////////////////////
// A simple struct storing a UUID string and its assigned index
typedef struct {
    const char* uuid_str;
    uint16_t    index;
} ServCharMapEntry;

// Global static variables: the mapping table and the next index to assign
// Assume the characteristic count stays below a preset limit, e.g. 32
#define MAX_SERVS 10
#define MAX_CHARS 32
static ServCharMapEntry serv_map[MAX_SERVS];
static ServCharMapEntry char_map[MAX_CHARS];
static uint16_t serv_count = 0;
static uint16_t char_count = 0;

static uint16_t _add_serv_info(const char *serv_uuid_str) {
    // Store the UUID and index in the mapping table
    uint16_t service_index = 0xFFFF;
    if (serv_count < MAX_SERVS) {
        service_index = serv_count;

        serv_map[serv_count].uuid_str = g_strdup(serv_uuid_str); // copy the string, as it is released later
        serv_map[serv_count].index = service_index; 
        log_debug("SET GATT", "serv uuid index: %d", serv_count);
        serv_count++;
    } else {
        log_error("SET GATT", "Max service count exceeded!");
    }
    return service_index;
}
static uint16_t _add_char_info(const char *char_uuid_str) {
    // Store the UUID and index in the mapping table
    uint16_t characteristic_index = 0xFFFF;
    if (char_count < MAX_CHARS) {
        characteristic_index = char_count+serv_count*10;   // two digits: tens = service index, units = characteristic index

        char_map[char_count].uuid_str = g_strdup(char_uuid_str); // copy the string, as char_uuid_str is released later
        char_map[char_count].index = characteristic_index; 
        log_debug("SET GATT", "char uuid index: %d", char_count);
        char_count++;
    } else {
        log_error("SET GATT", "Max characteristic count exceeded!");
    }
    return characteristic_index;
}
static uint16_t _get_serv_index_by_uuid(const char *serv_uuid) {
    uint16_t service_index = 0xFFFF; // use an invalid value as the default

    // Walk the mapping table to look up the UUID
    for (int i = 0; i < serv_count; i++) {
        if (g_strcmp0(serv_map[i].uuid_str, serv_uuid) == 0) {
            service_index = serv_map[i].index;
            break;
        }
    }

    return service_index;
}
static uint16_t _get_char_index_by_uuid(const char *char_uuid) {
    uint16_t characteristic_index = 0xFFFF; // use an invalid value as the default

    // Walk the mapping table to look up the UUID
    for (int i = 0; i < char_count; i++) {
        if (g_strcmp0(char_map[i].uuid_str, char_uuid) == 0) {
            characteristic_index = char_map[i].index;
            break;
        }
    }

    return characteristic_index;
}
static const char * _get_serv_uuid_by_char_index(uint16_t char_index) {
    uint16_t serv_index = char_index / 10;
    for (int i = 0; i < serv_count; i++) {
        if (serv_map[i].index == serv_index) {
            return serv_map[i].uuid_str;
        }
    }
    return NULL;
}
static const char * _get_char_uuid_by_char_index(uint16_t char_index) {
    for (int i = 0; i < char_count; i++) {
        if (char_map[i].index == char_index) {
            return char_map[i].uuid_str;
        }
    }
    return NULL;   
}
////////////////////////////////////////////////

static void on_powered_state_changed(Adapter *adapter, gboolean state) {
    log_debug(TAG, "powered '%s' (%s)", state ? "on" : "off", binc_adapter_get_path(adapter));
}

#define BLE_CONN_HANDLE 0x0001
static Device *sg_device = NULL;
static void on_central_state_changed(Adapter *adapter, Device *device) {
    char *deviceToString = binc_device_to_string(device);
    log_debug(TAG, deviceToString);
    g_free(deviceToString);

    log_debug(TAG, "remote central %s is %s", binc_device_get_address(device), binc_device_get_connection_state_name(device));
    ConnectionState state = binc_device_get_connection_state(device);
    if (state == BINC_CONNECTED) {
        sg_device = device;
        binc_adapter_stop_advertising(adapter, advertisement);
    } else if (state == BINC_DISCONNECTED){
        sg_device = NULL;
        // binc_adapter_start_advertising(adapter, advertisement);
        // Stopping advertising after a connect is handled by the lower layer; starting it after a disconnect is handled by the upper layer
    }
    

    // callback
    CORE_BLE_GAP_PARAMS_EVT_T event;
    memset(&event, 0, SIZEOF(CORE_BLE_GAP_PARAMS_EVT_T));
     
    event.result = 0;
    if (state == BINC_CONNECTED) {
        event.type = CORE_BLE_GAP_EVT_CONNECT;
    } else if (state == BINC_DISCONNECTED){
        event.type = CORE_BLE_GAP_EVT_DISCONNECT;
    }
    event.conn_handle = BLE_CONN_HANDLE;
    event.gap_event.connect.role = CORE_BLE_ROLE_SERVER;

    if (__gap_evt_cb) {
        __gap_evt_cb(&event);
    }
}

// This function is called when a read is done
// Use this to set the characteristic value if it is not set or to reject the read request
static const char *on_local_char_read(const Application *application, const char *address, const char *service_uuid,
                        const char *char_uuid) {
    const guint8 bytes[] = {0x06, 0x6f, 0x01, 0x00, 0xff, 0xe6, 0x07, 0x03, 0x03, 0x10, 0x04, 0x00, 0x01};
    GByteArray *byteArray = g_byte_array_sized_new(sizeof(bytes));
    g_byte_array_append(byteArray, bytes, sizeof(bytes));
    binc_application_set_char_value(application, service_uuid, char_uuid, byteArray);
    g_byte_array_free(byteArray, TRUE);

    CORE_BLE_GATT_PARAMS_EVT_T gatt_evt;
    memset(&gatt_evt, 0, sizeof(CORE_BLE_GATT_PARAMS_EVT_T));
    gatt_evt.type = CORE_BLE_GATT_EVT_READ_CHAR_VALUE;
    gatt_evt.conn_handle = 0;
    gatt_evt.result = 0;
    gatt_evt.gatt_event.char_read.char_handle = _get_char_index_by_uuid(char_uuid);
    gatt_evt.gatt_event.char_read.offset = 0;

    if (__gatt_evt_cb != NULL) {
        __gatt_evt_cb(&gatt_evt);
    }

    return NULL;
}

// This function should be used to validate or reject a write request
static const char *on_local_char_write(const Application *application, const char *address, const char *service_uuid,
                          const char *char_uuid, GByteArray *byteArray) {
    GString *result = g_byte_array_as_hex(byteArray);
    log_debug(TAG, "write request characteristic <%s> with value <%s>", char_uuid, result->str);
    g_string_free(result, TRUE);

    CORE_BLE_GATT_PARAMS_EVT_T gatt_evt;
    memset(&gatt_evt, 0, sizeof(CORE_BLE_GATT_PARAMS_EVT_T));
    gatt_evt.type = CORE_BLE_GATT_EVT_WRITE_REQ;
    gatt_evt.conn_handle = 0;
    gatt_evt.result = 0;
    gatt_evt.gatt_event.write_report.char_handle = _get_char_index_by_uuid(char_uuid);
    gatt_evt.gatt_event.write_report.report.p_data = byteArray->data;
    gatt_evt.gatt_event.write_report.report.len = byteArray->len;

    if (__gatt_evt_cb != NULL) {
        __gatt_evt_cb(&gatt_evt);
    }

    return NULL;
}

// This function is called after a write request was validates and the characteristic value was set
static void on_local_char_updated(const Application *application, const char *service_uuid,
                           const char *char_uuid, GByteArray *byteArray) {
    GString *result = g_byte_array_as_hex(byteArray);
    log_debug(TAG, "characteristic <%s> updated to <%s>", char_uuid, result->str);
    g_string_free(result, TRUE);
}

static void on_local_char_start_notify(const Application *application, const char *service_uuid, const char *char_uuid) {
    log_debug(TAG, "on start notify");
}

static void on_local_char_stop_notify(const Application *application, const char *service_uuid, const char *char_uuid) {
    log_debug(TAG, "on stop notify");
}

static gboolean on_request_authorization(Device *device) {
    log_debug(TAG, "requesting authorization for '%s", binc_device_get_name(device));
    return TRUE;
}

static guint32 on_request_passkey(Device *device) {
    guint32 pass = 000000;
    log_debug(TAG, "requesting passkey for '%s", binc_device_get_name(device));
    log_debug(TAG, "Enter 6 digit pin code: ");
    int result = fscanf(stdin, "%d", &pass);
    if (result != 1) {
        log_debug(TAG, "didn't read a pin code");
    }
    return pass;
}

static gboolean callback(gpointer data) {
    if (agent != NULL) {
        binc_agent_free(agent);
        agent = NULL;
    }

    if (default_adapter != NULL) {
        if (app != NULL) {
            binc_adapter_unregister_application(default_adapter, app);
            binc_application_free(app);
            app = NULL;
        }

        if (advertisement != NULL) {
            binc_adapter_stop_advertising(default_adapter, advertisement);
            binc_advertisement_free(advertisement);
            advertisement = NULL;
        }

        binc_adapter_free(default_adapter);
        default_adapter = NULL;
    } else {
        if (app != NULL) {
            binc_application_free(app);
            app = NULL;
        }
        if (advertisement != NULL) {
            binc_advertisement_free(advertisement);
            advertisement = NULL;
        }
    }

    if (data != NULL) {
        g_main_loop_quit((GMainLoop *) data);
    }
    return FALSE;
}

static void cleanup_handler(int signo) {
    if (signo == SIGINT) {
        if (cleanup_in_progress) {
            _exit(0);
        }
        cleanup_in_progress = 1;
        log_error(TAG, "received SIGINT");
        callback(loop);
        _exit(0);
    }
}

////////////////////////////////////////////////////////////////////////////////////
// 2. GAP

void bluez_inc_start_adv(void){
    log_debug("START ADV", "begin");

    // Try to get the currently running advertisement object
    advertisement = binc_adapter_get_advertisement(default_adapter);
    log_debug("UPDATE ADV", "get advertisement=%x", advertisement);

    if (advertisement == NULL) {
        log_debug("UPDATE ADV", "Advertisement object is not created. Creating one.");
        advertisement = binc_advertisement_create();
        if (advertisement == NULL) {
            log_debug("UPDATE ADV", "Failed to create advertisement object.");
            return;
        }
    }
   
    
    // If advertising is running, stop it first
    //binc_adapter_stop_advertising(default_adapter, advertisement);
    // Restart advertising to apply the new configuration
    binc_adapter_start_advertising(default_adapter, advertisement);
    log_debug("UPDATE ADV", "end");   
}

void bluez_inc_disconnect(void){
    log_debug("DISCONNECT", "begin");
    if (sg_device != NULL) {
        binc_device_disconnect(sg_device);
    }
}

////////////////////////////////////////////////////////////////////////////////////
// 3. GATT
// Helper: convert CORE_BLE_UUID_T to a C string (dynamic allocation, safer)
/* 36 canonical UUID characters + NUL; the 32-bit form expands to 40 characters
 * ("0000" + 8 hex + the 28-character base suffix), so 40 was one byte short and
 * snprintf() truncated it. */
#define UUID_STR_SIZE 41

char *uuid_to_string(const CORE_BLE_UUID_T* p_uuid) {
    // Dynamically allocate memory; the caller is responsible for freeing it
    char* uuid_str = (char*)malloc(UUID_STR_SIZE);
    if (!uuid_str) {
        return NULL;
    }
    memset(uuid_str, 0, UUID_STR_SIZE);

    switch (p_uuid->uuid_type) {
        case CORE_BLE_UUID_TYPE_16:
            snprintf(uuid_str, UUID_STR_SIZE, "0000%04x-0000-1000-8000-00805f9b34fb", p_uuid->uuid.uuid16);
            break;
        case CORE_BLE_UUID_TYPE_32:
            snprintf(uuid_str, UUID_STR_SIZE, "0000%08x-0000-1000-8000-00805f9b34fb", p_uuid->uuid.uuid32);
            break;
        case CORE_BLE_UUID_TYPE_128:
            snprintf(uuid_str, UUID_STR_SIZE, "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
                     p_uuid->uuid.uuid128[15], p_uuid->uuid.uuid128[14], p_uuid->uuid.uuid128[13], p_uuid->uuid.uuid128[12],
                     p_uuid->uuid.uuid128[11], p_uuid->uuid.uuid128[10],
                     p_uuid->uuid.uuid128[9], p_uuid->uuid.uuid128[8],
                     p_uuid->uuid.uuid128[7], p_uuid->uuid.uuid128[6],
                     p_uuid->uuid.uuid128[5], p_uuid->uuid.uuid128[4], p_uuid->uuid.uuid128[3], p_uuid->uuid.uuid128[2], p_uuid->uuid.uuid128[1], p_uuid->uuid.uuid128[0]);
            break;
        default:
            free(uuid_str);
            return NULL;
    }
    return uuid_str;
}

// Implement GATT setup based on the p_service contents
void bluez_inc_add_gatt(CORE_BLE_GATTS_PARAMS_T *p_service) {
    log_debug("SET GATT", "begin");

    // Create the application instance
    app = binc_create_application(default_adapter);
    if (!app) {
        log_debug("SET GATT", "Failed to create application");
        return;
    }

    // Iterate over every service
    for (int i = 0; i < p_service->svc_num; i++) {
        CORE_BLE_SERVICE_PARAMS_T *p_svc = &(p_service->p_service[i]);

        // Convert the service UUID to a string
        char *service_uuid_str = uuid_to_string(&p_svc->svc_uuid);
        if (!service_uuid_str) {
            log_debug("SET GATT", "Invalid UUID type for service %d", i);
            continue;
        }

        // Add the service
        printf("service_uuid_str:%s\n", service_uuid_str);
        if (binc_application_add_service(app, service_uuid_str) != 0) {
            log_debug("SET GATT", "Failed to add service with UUID: %s", service_uuid_str);
            free(service_uuid_str); 
            continue;
        }

        // Iterate over every characteristic of the current service
        for (int j = 0; j < p_svc->char_num; j++) {
            CORE_BLE_CHAR_PARAMS_T *p_char = &(p_svc->p_char[j]);

            // Convert the characteristic UUID to a string
            char *char_uuid_str = uuid_to_string(&p_char->char_uuid);
            if (!char_uuid_str) {
                log_debug("SET GATT", "Invalid UUID type for characteristic %d of service %s", j, service_uuid_str);
                continue;
            }

            // Add the characteristic. Assume the p_char->property bit definitions are compatible with the binc library.
            log_debug("SET GATT", "add char_uuid_str:%s -> service_uuid_str:%s, property:%x", char_uuid_str, service_uuid_str, p_char->property);
            if (binc_application_add_characteristic(app, service_uuid_str, char_uuid_str, p_char->property) != 0) {
                log_debug("SET GATT", "Failed to add characteristic with UUID: %s", char_uuid_str);
                free(char_uuid_str);
                continue;
            }

            // Store the UUID and index in the mapping table
            // Also hand the index to the upper layer as a handle (via the p_svc pointer)
            p_svc->p_char[j].handle = _add_char_info(char_uuid_str);

            free(char_uuid_str);
        }

        // Store the SERVICE and its index in the mapping table
        _add_serv_info(service_uuid_str);
        free(service_uuid_str); 
    }

    // Set the callback
    binc_application_set_char_read_cb(app, &on_local_char_read);
    binc_application_set_char_write_cb(app, &on_local_char_write);
    binc_application_set_char_start_notify_cb(app, &on_local_char_start_notify);
    binc_application_set_char_stop_notify_cb(app, &on_local_char_stop_notify);
    binc_application_set_char_updated_cb(app, &on_local_char_updated);

    // Register the application with the adapter
    binc_adapter_register_application(default_adapter, app);

    log_debug("SET GATT", "end");
}

int bluez_inc_gatt_value_notify(uint16_t conn_handle, uint16_t char_handle, uint8_t *p_data, uint16_t length) {
    const char * serv_uuid_str = _get_serv_uuid_by_char_index(char_handle);
    const char * char_uuid_str = _get_char_uuid_by_char_index(char_handle);
    if (serv_uuid_str && char_uuid_str) {
        log_debug("GATT_NOTIFY", "serv_uuid=%s, char_uuid=%s", serv_uuid_str, char_uuid_str);
        GByteArray *byteArray = g_byte_array_sized_new(length);
        g_byte_array_append(byteArray, p_data, length);
        binc_application_notify(app, serv_uuid_str, char_uuid_str, byteArray);
        g_byte_array_free(byteArray, TRUE);   
        return 0;//OPRT_OK
    }
    return -2;//OPRT_INVALID_PARM
}

////////////////////////////////////////////////////////////////////////////////////
// 1. Initialization
static GDBusConnection *dbusConnection = NULL;
static pthread_t my_thread_id;
void* _thread_function(void* arg) {
    // Start the mainloop
    g_main_loop_run(loop);

    // Clean up mainloop
    g_main_loop_unref(loop);

    // Disconnect from DBus
    g_dbus_connection_close_sync(dbusConnection, NULL, NULL);
    g_object_unref(dbusConnection);
    return NULL;
}

static bool is_bluez_inc_init = false;
void bluez_inc_init(CORE_BLE_GAP_EVT_FUNC_CB gap_evt_cb, CORE_BLE_GATT_EVT_FUNC_CB gatt_evt_cb){
    if (is_bluez_inc_init) return;
    is_bluez_inc_init = true;

    __gap_evt_cb = gap_evt_cb;
    __gatt_evt_cb = gatt_evt_cb;

    // Get a DBus connection
    dbusConnection = g_bus_get_sync(G_BUS_TYPE_SYSTEM, NULL, NULL);

    // Setup handler for CTRL+C
    if (signal(SIGINT, cleanup_handler) == SIG_ERR)
        log_error(TAG, "can't catch SIGINT");

    // Setup mainloop
    loop = g_main_loop_new(NULL, FALSE);

    // Get the default default_adapter
    default_adapter = binc_adapter_get_default(dbusConnection);

    if (default_adapter != NULL) {
        log_debug(TAG, "using default_adapter '%s'", binc_adapter_get_path(default_adapter));

        // Make sure the adapter is on
        // binc_adapter_power_off(default_adapter);
        binc_adapter_set_powered_state_cb(default_adapter, &on_powered_state_changed);
        if (!binc_adapter_get_powered_state(default_adapter)) {
            binc_adapter_power_on(default_adapter);
        }

        // Register an agent and set callbacks
        // agent = binc_agent_create(default_adapter, "/org/bluez/BincAgent", KEYBOARD_DISPLAY);
        // binc_agent_set_request_authorization_cb(agent, &on_request_authorization);
        // binc_agent_set_request_passkey_cb(agent, &on_request_passkey);

        // Setup remote central connection state callback
        binc_adapter_set_remote_central_cb(default_adapter, &on_central_state_changed);

        log_debug(TAG, "Stack Init OK");
    } else {
        log_debug(TAG, "No default_adapter found");
    }

    // Bail out after some time
    // Release resources after 600 s
    // g_timeout_add_seconds(600, callback, loop);

    // Using thread to run mainloop
    pthread_create(&my_thread_id, NULL, _thread_function, NULL);
}

void bluez_inc_deinit(void){
    
}
