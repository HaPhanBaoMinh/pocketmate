/* Pocketmate ANCS probe. GAP/GATT setup informed by Espressif's ble_ancs
 * example at ESP-IDF v5.5.1 (Unlicense OR CC0-1.0). Protocol handling,
 * bounded request queue and the application state machine are local code.
 */
#include <inttypes.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_gap_ble_api.h"
#include "esp_gattc_api.h"
#include "esp_gatt_common_api.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "cJSON.h"
#include "ancs_protocol.h"
#include "display.h"

#define TAG "POCKETMATE"
#define NAME "Pocketmate"
#define COUNT(a) (sizeof(a) / sizeof((a)[0]))
static const uint8_t ancs_uuid[16] = {0xd0,0x00,0x2d,0x12,0x1e,0x4b,0x0f,0xa4,0x99,0x4e,0xce,0xb5,0x31,0xf4,0x05,0x79};
static const uint8_t ns_uuid[16] = {0xbd,0x1d,0xa2,0x99,0xe6,0x25,0x58,0x8c,0xd9,0x42,0x01,0x63,0x0d,0x12,0xbf,0x9f};
static const uint8_t ds_uuid[16] = {0xfb,0x7b,0x7c,0xce,0x6a,0xb3,0x44,0xbe,0xb5,0x4b,0xd6,0x24,0xe9,0xc6,0xea,0x22};
static const uint8_t cp_uuid[16] = {0xd9,0xd9,0xaa,0xfd,0xbd,0x9b,0x21,0x98,0xa8,0x49,0xe1,0x45,0xf3,0xd8,0xd1,0x69};
/* Flags, ANCS service solicitation, then the HID service UUID (0x1812) and Generic HID
 * appearance. iOS Settings > Bluetooth only lists LE peripherals it recognises, and
 * solicitation alone was not enough on iOS 18; Espressif's ble_ancs example advertises
 * HID for the same reason. No HID GATT service is implemented. */
static uint8_t adv_data[] = {2,0x01,0x06,17,0x15,
    0xd0,0x00,0x2d,0x12,0x1e,0x4b,0x0f,0xa4,0x99,0x4e,0xce,0xb5,0x31,0xf4,0x05,0x79,
    3,0x03,0x12,0x18,
    3,0x19,0xc0,0x03};
static esp_ble_adv_params_t adv_params = {
    .adv_int_min=0x100, .adv_int_max=0x100, .adv_type=ADV_TYPE_IND,
    .own_addr_type=BLE_ADDR_TYPE_PUBLIC, .channel_map=ADV_CHNL_ALL,
    .adv_filter_policy=ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
};

/* All state changes happen on app_main, never in a timer or BT callback. */
typedef struct {
    bool gap;
    int event;
    esp_gatt_if_t interface;
    union { esp_ble_gap_cb_param_t gap; esp_ble_gattc_cb_param_t gatt; } p;
    uint8_t bytes[512];
} message_t;
static QueueHandle_t events;
static atomic_bool queue_overflow;
static esp_gatt_if_t interface = ESP_GATT_IF_NONE;
static esp_bd_addr_t peer;
static uint16_t conn, service_start, service_end, ns_handle, ds_handle, cp_handle;
static uint16_t ns_cccd, ds_cccd;
static bool connected, opened, authenticated, discovering, ready;
static unsigned adv_pending;
static int64_t retry_at, phase_deadline, request_deadline;
static ancs_event_t pending[16], current;
static size_t pending_count;
static bool busy, text_phase, canceled, write_done, response_done;
static ancs_response_t response;
static uint32_t maps_uids[16];
static size_t maps_count;

static ui_model_t ui;
static bool ui_dirty;
static int64_t ui_rendered_at, maps_updated_at;
static void state(const char *s) { ESP_LOGI(TAG, "STATE %s", s); }
/* Short Vietnamese status for the LCD top bar; the USB log keeps the detailed English state. */
static void screen(const char *status)
{
    snprintf(ui.status, sizeof(ui.status), "%s", status);
    ui_dirty = true;
}
static void refresh_screen(void)
{
    int64_t now = esp_timer_get_time();
    if (ui.stale) {
        uint32_t age = (uint32_t)((now - maps_updated_at) / 1000000);
        if (age != ui.age_seconds) { ui.age_seconds = age; ui_dirty = true; }
    }
    if (ui_dirty && now - ui_rendered_at > 150000) {
        display_present(&ui);
        ui_rendered_at = esp_timer_get_time();
        ui_dirty = false;
    }
}
static void tick(void);
static void finish_response(void);

static void gap_callback(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *p)
{
    message_t msg = {.gap=true, .event=event, .p.gap=*p};
    if (xQueueSend(events, &msg, 0) != pdTRUE) atomic_store(&queue_overflow, true);
}
static void gatt_callback(esp_gattc_cb_event_t event, esp_gatt_if_t gatt_if, esp_ble_gattc_cb_param_t *p)
{
    message_t msg = {.event=event, .interface=gatt_if, .p.gatt=*p};
    if (event == ESP_GATTC_NOTIFY_EVT) {
        if (p->notify.value_len > sizeof(msg.bytes)) {
            atomic_store(&queue_overflow, true);
            return;
        }
        memcpy(msg.bytes, p->notify.value, p->notify.value_len);
    }
    if (xQueueSend(events, &msg, 0) != pdTRUE) atomic_store(&queue_overflow, true);
}
static void clear_session(void)
{
    connected=opened=authenticated=discovering=ready=false;
    busy=canceled=write_done=response_done=false;
    pending_count=maps_count=0;
    service_start=service_end=ns_handle=ds_handle=cp_handle=ns_cccd=ds_cccd=0;
    retry_at=phase_deadline=request_deadline=0;
    memset(&response, 0, sizeof(response));
}
static void abort_link(const char *why)
{
    ESP_LOGW(TAG, "%s; discard this session and reconnect", why);
    ready=false;
    busy=false;
    retry_at=phase_deadline=request_deadline=0;
    if (connected) esp_ble_gap_disconnect(peer);
}
static void advertise(void)
{
    esp_err_t e=esp_ble_gap_start_advertising(&adv_params);
    if (e != ESP_OK) ESP_LOGE(TAG, "start advertising: %s", esp_err_to_name(e));
}
static bool same_uuid(const esp_bt_uuid_t *uuid, const uint8_t *bytes)
{
    return uuid->len == ESP_UUID_LEN_128 && memcmp(uuid->uuid.uuid128, bytes, 16) == 0;
}
static void discover(void)
{
    if (!connected || !opened || !authenticated || discovering || ready) return;
    esp_bt_uuid_t uuid = {.len=ESP_UUID_LEN_128};
    memcpy(uuid.uuid.uuid128, ancs_uuid, sizeof(ancs_uuid));
    service_start=service_end=ns_handle=ds_handle=cp_handle=ns_cccd=ds_cccd=0;
    discovering=true;
    retry_at=0;
    phase_deadline=esp_timer_get_time()+15000000;
    state("discovering_ancs");
    if (esp_ble_gattc_search_service(interface, conn, &uuid) != ESP_OK) abort_link("service search failed");
}
static bool find_char(const uint8_t *bytes, uint16_t *handle)
{
    esp_bt_uuid_t uuid = {.len=ESP_UUID_LEN_128};
    memcpy(uuid.uuid.uuid128, bytes, 16);
    esp_gattc_char_elem_t element;
    uint16_t count=1;
    if (esp_ble_gattc_get_char_by_uuid(interface,conn,service_start,service_end,uuid,&element,&count) != ESP_GATT_OK || count != 1) return false;
    *handle=element.char_handle;
    return true;
}
static void subscribe(uint16_t handle)
{
    phase_deadline=esp_timer_get_time()+15000000;
    if (esp_ble_gattc_register_for_notify(interface,peer,handle) != ESP_OK) abort_link("register notify failed");
}
static void write_cccd(uint16_t handle)
{
    esp_bt_uuid_t uuid = {.len=ESP_UUID_LEN_16, .uuid.uuid16=ESP_GATT_UUID_CHAR_CLIENT_CONFIG};
    esp_gattc_descr_elem_t element;
    uint16_t count=1;
    if (esp_ble_gattc_get_descr_by_char_handle(interface,conn,handle,uuid,&element,&count) != ESP_GATT_OK || count != 1) {
        abort_link("missing CCCD"); return;
    }
    if (handle == ds_handle) ds_cccd=element.handle;
    else ns_cccd=element.handle;
    uint8_t enable[] = {1,0};
    if (esp_ble_gattc_write_char_descr(interface,conn,element.handle,2,enable,ESP_GATT_WRITE_TYPE_RSP,ESP_GATT_AUTH_REQ_NONE) != ESP_OK) abort_link("subscribe failed");
}
static void request(bool text)
{
    uint8_t command[14];
    size_t n=ancs_request(command,sizeof(command),current.uid,text);
    text_phase=text;
    write_done=response_done=false;
    ancs_response_begin(&response,current.uid,text);
    busy=true;
    request_deadline=esp_timer_get_time()+8000000;
    if (esp_ble_gattc_write_char(interface,conn,cp_handle,n,command,ESP_GATT_WRITE_TYPE_RSP,ESP_GATT_AUTH_REQ_NONE) != ESP_OK) abort_link("attribute request failed");
}
static void next_request(void)
{
    if (!ready || busy || !pending_count) return;
    current=pending[0];
    memmove(pending,pending+1,(--pending_count)*sizeof(*pending));
    canceled=false;
    request(false); /* First fetch ONLY the app identifier. */
}
static bool maps_known(uint32_t uid)
{
    for (size_t i=0;i<maps_count;i++) if (maps_uids[i]==uid) return true;
    return false;
}
static void print_notification(void)
{
    char title[ANCS_TITLE_MAX+1], subtitle[ANCS_TITLE_MAX+1], body[ANCS_MESSAGE_MAX+1];
    if (!ancs_response_text(&response,ANCS_TITLE,title,sizeof(title)) ||
        !ancs_response_text(&response,ANCS_SUBTITLE,subtitle,sizeof(subtitle)) ||
        !ancs_response_text(&response,ANCS_MESSAGE,body,sizeof(body))) return;
    cJSON *obj=cJSON_CreateObject();
    if (!obj) return;
    cJSON_AddStringToObject(obj,"app","com.google.Maps");
    cJSON_AddNumberToObject(obj,"uid",current.uid);
    cJSON_AddStringToObject(obj,"event",current.event==ANCS_ADDED ? "added" : "modified");
    cJSON_AddStringToObject(obj,"title",title);
    cJSON_AddStringToObject(obj,"subtitle",subtitle);
    cJSON_AddStringToObject(obj,"message",body);
    char *json=cJSON_PrintUnformatted(obj);
    if (json) { printf("MAPS %s\n",json); cJSON_free(json); }
    cJSON_Delete(obj);
    snprintf(ui.title,sizeof(ui.title),"%s",title);
    snprintf(ui.subtitle,sizeof(ui.subtitle),"%s",subtitle);
    snprintf(ui.message,sizeof(ui.message),"%s",body);
    ui.has_maps=true; ui.stale=false; ui.age_seconds=0;
    maps_updated_at=esp_timer_get_time();
    ui_dirty=true;
    if (!maps_known(current.uid)) {
        if (maps_count == COUNT(maps_uids)) {
            memmove(maps_uids,maps_uids+1,(maps_count-1)*sizeof(*maps_uids));
            maps_count--;
        }
        maps_uids[maps_count++]=current.uid;
    }
}
static void finish_response(void)
{
    /* ATT write ACK and Data Source may arrive in either order. */
    if (!busy || !write_done || !response_done) return;
    if (!canceled && !text_phase) {
        char app[256];
        if (ancs_response_text(&response,ANCS_APP,app,sizeof(app)) && strcmp(app,"com.google.Maps")==0) {
            request(true); return;
        }
        /* Don't fetch or log titles/messages from unrelated apps. */
        ESP_LOGI(TAG,"ANCS uid=%" PRIu32 " received (other app, text not requested)",current.uid);
        if (current.event==ANCS_ADDED) { ui.other_count++; ui_dirty=true; }
    } else if (!canceled) print_notification();
    busy=false;
    request_deadline=0;
    next_request();
}
static void notification_source(const uint8_t *p, size_t n)
{
    ancs_event_t e;
    if (!ancs_decode_event(p,n,&e)) { abort_link("invalid Notification Source packet"); return; }
    ESP_LOGI(TAG,"ANCS event=%u category=%u uid=%" PRIu32,e.event,e.category,e.uid);
    if (e.event==ANCS_REMOVED) {
        for (size_t i=0;i<pending_count;) {
            if (pending[i].uid==e.uid) {
                memmove(pending+i,pending+i+1,(pending_count-i-1)*sizeof(*pending));
                pending_count--;
            } else i++;
        }
        if (busy && current.uid==e.uid) canceled=true;
        if (maps_known(e.uid)) {
            printf("MAPS {\"event\":\"removed\",\"uid\":%" PRIu32 "}\n",e.uid);
            for (size_t i=0;i<maps_count;i++) if (maps_uids[i]==e.uid) {
                maps_uids[i]=maps_uids[--maps_count]; break;
            }
        }
        return;
    }
    for (size_t i=0;i<pending_count;i++) if (pending[i].uid==e.uid) { pending[i]=e; return; }
    if (pending_count==COUNT(pending)) {
        ESP_LOGW(TAG,"notification queue full; drop oldest pending UID");
        memmove(pending,pending+1,(--pending_count)*sizeof(*pending));
    }
    pending[pending_count++]=e;
    next_request();
}
static void handle_gap(int event, esp_ble_gap_cb_param_t *p)
{
    switch (event) {
    case ESP_GAP_BLE_ADV_DATA_RAW_SET_COMPLETE_EVT:
        if (p->adv_data_raw_cmpl.status != ESP_BT_STATUS_SUCCESS) { state("advertisement_config_failed"); break; }
        adv_pending &= ~1u;
        if (!adv_pending) advertise();
        break;
    case ESP_GAP_BLE_SCAN_RSP_DATA_SET_COMPLETE_EVT:
        if (p->scan_rsp_data_cmpl.status != ESP_BT_STATUS_SUCCESS) { state("scan_response_config_failed"); break; }
        adv_pending &= ~2u;
        if (!adv_pending) advertise();
        break;
    case ESP_GAP_BLE_ADV_START_COMPLETE_EVT:
        state(p->adv_start_cmpl.status==ESP_BT_STATUS_SUCCESS ? "advertising: open iPhone Settings > Bluetooth > Pocketmate" : "advertising_failed");
        screen(p->adv_start_cmpl.status==ESP_BT_STATUS_SUCCESS ? "Chờ iPhone: Bluetooth > Pocketmate" : "Lỗi quảng bá BLE");
        break;
    case ESP_GAP_BLE_SEC_REQ_EVT:
        esp_ble_gap_security_rsp(p->ble_security.ble_req.bd_addr,true);
        break;
    case ESP_GAP_BLE_PASSKEY_NOTIF_EVT:
        ESP_LOGI(TAG,"PAIRING_CODE %06" PRIu32 " (enter on iPhone)",p->ble_security.key_notif.passkey);
        snprintf(ui.passkey,sizeof(ui.passkey),"%06" PRIu32,p->ble_security.key_notif.passkey);
        screen("Ghép nối iPhone");
        break;
    case ESP_GAP_BLE_NC_REQ_EVT:
        /* IO_OUT should use passkey entry, never silently approve comparison. */
        esp_ble_confirm_reply(p->ble_security.ble_req.bd_addr,false);
        break;
    case ESP_GAP_BLE_PASSKEY_REQ_EVT:
        esp_ble_passkey_reply(p->ble_security.ble_req.bd_addr,false,0);
        break;
    case ESP_GAP_BLE_AUTH_CMPL_EVT:
        ui.passkey[0]=0; ui_dirty=true;
        if (!p->ble_security.auth_cmpl.success) {
            ESP_LOGW(TAG,"pairing failed: 0x%x",p->ble_security.auth_cmpl.fail_reason);
            screen("Ghép nối thất bại");
            abort_link("authentication failed");
            break;
        }
        authenticated=true;
        ESP_LOGI(TAG,"pairing auth_mode=0x%x (MITM %s)",p->ble_security.auth_cmpl.auth_mode,
                 (p->ble_security.auth_cmpl.auth_mode & ESP_LE_AUTH_REQ_MITM) ? "yes: passkey was used" : "no: Just Works, peer had no keyboard");
        state("paired: allow Share System Notifications on iPhone");
        screen("Đã ghép nối, bật Chia sẻ thông báo");
        discover();
        break;
    default: break;
    }
}
static void handle_gatt(int event, esp_gatt_if_t gatt_if, esp_ble_gattc_cb_param_t *p)
{
    if (event != ESP_GATTC_REG_EVT && gatt_if != interface && gatt_if != ESP_GATT_IF_NONE) return;
    switch (event) {
    case ESP_GATTC_REG_EVT: {
        if (p->reg.status != ESP_GATT_OK) { state("registration_failed"); break; }
        interface=gatt_if;
        ESP_ERROR_CHECK(esp_ble_gap_set_device_name(NAME));
        adv_pending=3;
        ESP_ERROR_CHECK(esp_ble_gap_config_adv_data_raw(adv_data,sizeof(adv_data)));
        esp_ble_adv_data_t scan={.set_scan_rsp=true,.include_name=true};
        ESP_ERROR_CHECK(esp_ble_gap_config_adv_data(&scan));
        break;
    }
    case ESP_GATTC_CONNECT_EVT: {
        if (connected) break;
        clear_session();
        connected=true;
        conn=p->connect.conn_id;
        memcpy(peer,p->connect.remote_bda,sizeof(peer));
        phase_deadline=esp_timer_get_time()+90000000;
        state("connected");
        /* iPhones use random (resolvable) addresses; a laptop usually shows a public one. */
        ESP_LOGI(TAG,"peer %02x:%02x:%02x:%02x:%02x:%02x addr_type=%d (%s)",peer[0],peer[1],peer[2],peer[3],peer[4],peer[5],
                 p->connect.ble_addr_type,p->connect.ble_addr_type==BLE_ADDR_TYPE_PUBLIC ? "public: likely a computer" : "random: likely a phone");
        screen("Đang kết nối iPhone…");
        esp_ble_gatt_creat_conn_params_t args={0};
        memcpy(args.remote_bda,peer,sizeof(peer));
        args.remote_addr_type=p->connect.ble_addr_type;
        args.own_addr_type=BLE_ADDR_TYPE_PUBLIC;
        args.is_direct=true;
        if (esp_ble_gattc_enh_open(interface,&args) != ESP_OK) abort_link("GATT open failed");
        break;
    }
    case ESP_GATTC_OPEN_EVT:
        if (p->open.status != ESP_GATT_OK) { abort_link("GATT open failed"); break; }
        opened=true;
        conn=p->open.conn_id;
        if (esp_ble_set_encryption(peer,ESP_BLE_SEC_ENCRYPT_MITM) != ESP_OK) { abort_link("encryption request failed"); break; }
        esp_ble_gattc_send_mtu_req(interface,conn);
        discover();
        break;
    case ESP_GATTC_CFG_MTU_EVT:
        ESP_LOGI(TAG,"ATT MTU=%u status=%u",p->cfg_mtu.mtu,p->cfg_mtu.status);
        break;
    case ESP_GATTC_SEARCH_RES_EVT:
        if (same_uuid(&p->search_res.srvc_id.uuid,ancs_uuid)) {
            service_start=p->search_res.start_handle;
            service_end=p->search_res.end_handle;
        }
        break;
    case ESP_GATTC_SEARCH_CMPL_EVT:
        discovering=false;
        if (p->search_cmpl.status != ESP_GATT_OK) { abort_link("ANCS search error"); break; }
        if (!service_start) {
            state("waiting_for_ancs: check Share System Notifications");
            screen("Bật Share System Notifications");
            phase_deadline=0;
            retry_at=esp_timer_get_time()+5000000;
            break;
        }
        if (!find_char(ns_uuid,&ns_handle) || !find_char(ds_uuid,&ds_handle) || !find_char(cp_uuid,&cp_handle)) {
            abort_link("ANCS characteristics missing"); break;
        }
        subscribe(ds_handle); /* Data Source must be enabled before NS. */
        break;
    case ESP_GATTC_REG_FOR_NOTIFY_EVT:
        if (p->reg_for_notify.status != ESP_GATT_OK) { abort_link("notify registration failed"); break; }
        write_cccd(p->reg_for_notify.handle);
        break;
    case ESP_GATTC_WRITE_DESCR_EVT:
        if (p->write.status != ESP_GATT_OK) { abort_link("CCCD write denied"); break; }
        if (p->write.handle==ds_cccd) subscribe(ns_handle);
        else if (p->write.handle==ns_cccd) {
            ready=true;
            phase_deadline=0;
            state("ancs_ready");
            screen("Pocketmate · đã kết nối");
            next_request();
        }
        break;
    case ESP_GATTC_NOTIFY_EVT:
        if (p->notify.conn_id != conn || !authenticated) break;
        if (p->notify.handle==ns_handle) notification_source(p->notify.value,p->notify.value_len);
        else if (p->notify.handle==ds_handle) {
            if (!busy) { abort_link("unsolicited Data Source response"); break; }
            int result=ancs_response_feed(&response,p->notify.value,p->notify.value_len);
            if (result<0) abort_link("invalid or oversized ANCS response");
            else if (result>0) { response_done=true; finish_response(); }
        }
        break;
    case ESP_GATTC_WRITE_CHAR_EVT:
        if (p->write.handle != cp_handle || !busy) break;
        if (p->write.status != ESP_GATT_OK) {
            /* A UID may disappear between NS and request. Disconnect also
             * ensures delayed response bytes cannot poison the next request. */
            abort_link("ANCS attribute write rejected"); break;
        }
        write_done=true;
        finish_response();
        break;
    case ESP_GATTC_SRVC_CHG_EVT:
        abort_link("iPhone services changed");
        break;
    case ESP_GATTC_DISCONNECT_EVT:
        ESP_LOGI(TAG,"disconnected reason=0x%x",p->disconnect.reason);
        clear_session();
        state("disconnected: previous navigation is no longer current");
        ui.passkey[0]=0;
        if (ui.has_maps) ui.stale=true;
        screen("Mất kết nối iPhone");
        advertise();
        break;
    default: break;
    }
}
static void tick(void)
{
    if (atomic_exchange(&queue_overflow,false)) {
        xQueueReset(events);
        abort_link("BLE event queue overflow");
    }
    int64_t now=esp_timer_get_time();
    if (busy && request_deadline && now>request_deadline) abort_link("ANCS response timeout");
    if (connected && phase_deadline && now>phase_deadline) abort_link("pairing/discovery timeout");
    if (retry_at && now>retry_at) discover();
    refresh_screen();
}
void app_main(void)
{
    state("boot: ANCS probe with ILI9341 status screen");
    esp_err_t lcd=display_init();
    if (lcd != ESP_OK) ESP_LOGE(TAG,"display init failed: %s (continuing without LCD)",esp_err_to_name(lcd));
    screen("Khởi động…");
    refresh_screen();
    esp_err_t result=nvs_flash_init();
    if (result==ESP_ERR_NVS_NO_FREE_PAGES || result==ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        result=nvs_flash_init();
    }
    ESP_ERROR_CHECK(result);
    events=xQueueCreate(24,sizeof(message_t));
    assert(events);
    ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT));
    esp_bt_controller_config_t config=BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_bt_controller_init(&config));
    ESP_ERROR_CHECK(esp_bt_controller_enable(ESP_BT_MODE_BLE));
    ESP_ERROR_CHECK(esp_bluedroid_init());
    ESP_ERROR_CHECK(esp_bluedroid_enable());
    ESP_ERROR_CHECK(esp_ble_gap_register_callback(gap_callback));
    ESP_ERROR_CHECK(esp_ble_gattc_register_callback(gatt_callback));
    uint8_t auth=ESP_LE_AUTH_REQ_SC_MITM_BOND, io=ESP_IO_CAP_OUT, key_size=16;
    uint8_t keys=ESP_BLE_ENC_KEY_MASK|ESP_BLE_ID_KEY_MASK;
    ESP_ERROR_CHECK(esp_ble_gap_set_security_param(ESP_BLE_SM_AUTHEN_REQ_MODE,&auth,sizeof(auth)));
    ESP_ERROR_CHECK(esp_ble_gap_set_security_param(ESP_BLE_SM_IOCAP_MODE,&io,sizeof(io)));
    ESP_ERROR_CHECK(esp_ble_gap_set_security_param(ESP_BLE_SM_MAX_KEY_SIZE,&key_size,sizeof(key_size)));
    ESP_ERROR_CHECK(esp_ble_gap_set_security_param(ESP_BLE_SM_SET_INIT_KEY,&keys,sizeof(keys)));
    ESP_ERROR_CHECK(esp_ble_gap_set_security_param(ESP_BLE_SM_SET_RSP_KEY,&keys,sizeof(keys)));
    ESP_ERROR_CHECK(esp_ble_gatt_set_local_mtu(247));
    ESP_ERROR_CHECK(esp_ble_gattc_app_register(0));
    while (true) {
        message_t msg;
        if (xQueueReceive(events,&msg,pdMS_TO_TICKS(200))==pdTRUE) {
            if (msg.gap) handle_gap(msg.event,&msg.p.gap);
            else {
                if (msg.event==ESP_GATTC_NOTIFY_EVT) msg.p.gatt.notify.value=msg.bytes;
                handle_gatt(msg.event,msg.interface,&msg.p.gatt);
            }
        }
        tick();
    }
}
