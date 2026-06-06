#include "app_host_gatt.h"
#include "../host_cmd/app_host_cmd.h"
#include "stack/ble/ble.h"
#include "stack/ble/ble_format.h"
#include "common/string.h"

#define HOST_GATT_NOTIFY_QUEUE_SIZE 8

enum {
    ATT_HANDLE_NONE = 0,
    ATT_HANDLE_GAP_SERVICE = 1,
    ATT_HANDLE_GAP_DEVICE_NAME_DECL,
    ATT_HANDLE_GAP_DEVICE_NAME_VALUE,
    ATT_HANDLE_GAP_APPEARANCE_DECL,
    ATT_HANDLE_GAP_APPEARANCE_VALUE,

    ATT_HANDLE_DEBUG_SERVICE,
    ATT_HANDLE_DEBUG_CMD_DECL,
    ATT_HANDLE_DEBUG_CMD_VALUE,
    ATT_HANDLE_DEBUG_RSP_DECL,
    ATT_HANDLE_DEBUG_RSP_VALUE,
    ATT_HANDLE_DEBUG_RSP_CCC,
    ATT_HANDLE_DEBUG_LOG_DECL,
    ATT_HANDLE_DEBUG_LOG_VALUE,
    ATT_HANDLE_DEBUG_LOG_CCC,
    ATT_HANDLE_DEBUG_EVT_DECL,
    ATT_HANDLE_DEBUG_EVT_VALUE,
    ATT_HANDLE_DEBUG_EVT_CCC,

    ATT_HANDLE_END,
};

typedef struct {
    u16 handle;
    u8 len;
    u8 data[APP_HOST_FRAME_MAX_PACKET_LEN];
} host_notify_item_t;

static const u16 s_uuid_primary_service = GATT_UUID_PRIMARY_SERVICE;
static const u16 s_uuid_character = GATT_UUID_CHARACTER;
static const u16 s_uuid_client_char_cfg = GATT_UUID_CLIENT_CHAR_CFG;
static const u16 s_uuid_gap_service = SERVICE_UUID_GENERIC_ACCESS;
static const u16 s_uuid_device_name = GATT_UUID_DEVICE_NAME;
static const u16 s_uuid_appearance = GATT_UUID_APPEARANCE;

static u8 s_device_name[] = "PENDANT";
static u16 s_appearance = 0;
static u8 s_device_name_decl[5] = {CHAR_PROP_READ, U16_LO(ATT_HANDLE_GAP_DEVICE_NAME_VALUE), U16_HI(ATT_HANDLE_GAP_DEVICE_NAME_VALUE),
                                   U16_LO(GATT_UUID_DEVICE_NAME), U16_HI(GATT_UUID_DEVICE_NAME)};
static u8 s_appearance_decl[5] = {CHAR_PROP_READ, U16_LO(ATT_HANDLE_GAP_APPEARANCE_VALUE), U16_HI(ATT_HANDLE_GAP_APPEARANCE_VALUE),
                                  U16_LO(GATT_UUID_APPEARANCE), U16_HI(GATT_UUID_APPEARANCE)};

static const u8 s_uuid_debug_service[16] = {0x44, 0x4E, 0x54, 0x50, 0x01, 0x00, 0x45, 0x4B, 0x59, 0x31, 0x44, 0x45, 0x56, 0x00, 0x00, 0x01};
static const u8 s_uuid_debug_cmd[16]     = {0x44, 0x4E, 0x54, 0x50, 0x02, 0x00, 0x45, 0x4B, 0x59, 0x31, 0x44, 0x45, 0x56, 0x00, 0x00, 0x01};
static const u8 s_uuid_debug_rsp[16]     = {0x44, 0x4E, 0x54, 0x50, 0x03, 0x00, 0x45, 0x4B, 0x59, 0x31, 0x44, 0x45, 0x56, 0x00, 0x00, 0x01};
static const u8 s_uuid_debug_log[16]     = {0x44, 0x4E, 0x54, 0x50, 0x04, 0x00, 0x45, 0x4B, 0x59, 0x31, 0x44, 0x45, 0x56, 0x00, 0x00, 0x01};
static const u8 s_uuid_debug_evt[16]     = {0x44, 0x4E, 0x54, 0x50, 0x05, 0x00, 0x45, 0x4B, 0x59, 0x31, 0x44, 0x45, 0x56, 0x00, 0x00, 0x01};

static u8 s_cmd_char_decl[19] = {CHAR_PROP_WRITE_WITHOUT_RSP | CHAR_PROP_WRITE, U16_LO(ATT_HANDLE_DEBUG_CMD_VALUE), U16_HI(ATT_HANDLE_DEBUG_CMD_VALUE),
                                 0x44, 0x4E, 0x54, 0x50, 0x02, 0x00, 0x45, 0x4B, 0x59, 0x31, 0x44, 0x45, 0x56, 0x00, 0x00, 0x01};
static u8 s_rsp_char_decl[19] = {CHAR_PROP_NOTIFY, U16_LO(ATT_HANDLE_DEBUG_RSP_VALUE), U16_HI(ATT_HANDLE_DEBUG_RSP_VALUE),
                                 0x44, 0x4E, 0x54, 0x50, 0x03, 0x00, 0x45, 0x4B, 0x59, 0x31, 0x44, 0x45, 0x56, 0x00, 0x00, 0x01};
static u8 s_log_char_decl[19] = {CHAR_PROP_NOTIFY, U16_LO(ATT_HANDLE_DEBUG_LOG_VALUE), U16_HI(ATT_HANDLE_DEBUG_LOG_VALUE),
                                 0x44, 0x4E, 0x54, 0x50, 0x04, 0x00, 0x45, 0x4B, 0x59, 0x31, 0x44, 0x45, 0x56, 0x00, 0x00, 0x01};
static u8 s_evt_char_decl[19] = {CHAR_PROP_NOTIFY, U16_LO(ATT_HANDLE_DEBUG_EVT_VALUE), U16_HI(ATT_HANDLE_DEBUG_EVT_VALUE),
                                 0x44, 0x4E, 0x54, 0x50, 0x05, 0x00, 0x45, 0x4B, 0x59, 0x31, 0x44, 0x45, 0x56, 0x00, 0x00, 0x01};

static u8 s_cmd_value[APP_HOST_FRAME_MAX_PACKET_LEN];
static u8 s_rsp_value[APP_HOST_FRAME_MAX_PACKET_LEN];
static u8 s_log_value[APP_HOST_FRAME_MAX_PACKET_LEN];
static u8 s_evt_value[APP_HOST_FRAME_MAX_PACKET_LEN];
static u16 s_rsp_ccc;
static u16 s_log_ccc;
static u16 s_evt_ccc;

static host_notify_item_t s_notify_queue[HOST_GATT_NOTIFY_QUEUE_SIZE];
static u8 s_q_head;
static u8 s_q_tail;
static u8 s_q_count;
static u8 s_connected;
static u16 s_conn_handle;

static int app_host_gatt_cmd_write_cb(u16 conn_handle, void *p);
static int app_host_gatt_ccc_write_cb(u16 conn_handle, void *p);

static attribute_t s_att_table[] = {
    {ATT_HANDLE_END - 1, 0, 0, 0, 0, 0, 0, 0},

    {5, ATT_PERMISSIONS_READ, 2, sizeof(s_uuid_gap_service), (u8 *)&s_uuid_primary_service, (u8 *)&s_uuid_gap_service, 0, 0},
    {0, ATT_PERMISSIONS_READ, 2, sizeof(s_device_name_decl), (u8 *)&s_uuid_character, s_device_name_decl, 0, 0},
    {0, ATT_PERMISSIONS_READ, 2, sizeof(s_device_name) - 1, (u8 *)&s_uuid_device_name, s_device_name, 0, 0},
    {0, ATT_PERMISSIONS_READ, 2, sizeof(s_appearance_decl), (u8 *)&s_uuid_character, s_appearance_decl, 0, 0},
    {0, ATT_PERMISSIONS_READ, 2, sizeof(s_appearance), (u8 *)&s_uuid_appearance, (u8 *)&s_appearance, 0, 0},

    {12, ATT_PERMISSIONS_READ, 2, sizeof(s_uuid_debug_service), (u8 *)&s_uuid_primary_service, (u8 *)s_uuid_debug_service, 0, 0},
    {0, ATT_PERMISSIONS_READ, 2, sizeof(s_cmd_char_decl), (u8 *)&s_uuid_character, s_cmd_char_decl, 0, 0},
    {0, ATT_PERMISSIONS_WRITE, 16, sizeof(s_cmd_value), (u8 *)s_uuid_debug_cmd, s_cmd_value, app_host_gatt_cmd_write_cb, 0},
    {0, ATT_PERMISSIONS_READ, 2, sizeof(s_rsp_char_decl), (u8 *)&s_uuid_character, s_rsp_char_decl, 0, 0},
    {0, ATT_PERMISSIONS_READ, 16, sizeof(s_rsp_value), (u8 *)s_uuid_debug_rsp, s_rsp_value, 0, 0},
    {0, ATT_PERMISSIONS_RDWR, 2, sizeof(s_rsp_ccc), (u8 *)&s_uuid_client_char_cfg, (u8 *)&s_rsp_ccc, app_host_gatt_ccc_write_cb, 0},
    {0, ATT_PERMISSIONS_READ, 2, sizeof(s_log_char_decl), (u8 *)&s_uuid_character, s_log_char_decl, 0, 0},
    {0, ATT_PERMISSIONS_READ, 16, sizeof(s_log_value), (u8 *)s_uuid_debug_log, s_log_value, 0, 0},
    {0, ATT_PERMISSIONS_RDWR, 2, sizeof(s_log_ccc), (u8 *)&s_uuid_client_char_cfg, (u8 *)&s_log_ccc, app_host_gatt_ccc_write_cb, 0},
    {0, ATT_PERMISSIONS_READ, 2, sizeof(s_evt_char_decl), (u8 *)&s_uuid_character, s_evt_char_decl, 0, 0},
    {0, ATT_PERMISSIONS_READ, 16, sizeof(s_evt_value), (u8 *)s_uuid_debug_evt, s_evt_value, 0, 0},
    {0, ATT_PERMISSIONS_RDWR, 2, sizeof(s_evt_ccc), (u8 *)&s_uuid_client_char_cfg, (u8 *)&s_evt_ccc, app_host_gatt_ccc_write_cb, 0},
};

static u8 app_host_gatt_notify_enabled(u16 handle)
{
    if (handle == ATT_HANDLE_DEBUG_RSP_VALUE) {
        return (s_rsp_ccc & 0x0001) ? 1 : 0;
    }
    if (handle == ATT_HANDLE_DEBUG_LOG_VALUE) {
        return (s_log_ccc & 0x0001) ? 1 : 0;
    }
    if (handle == ATT_HANDLE_DEBUG_EVT_VALUE) {
        return (s_evt_ccc & 0x0001) ? 1 : 0;
    }
    return 0;
}

static u16 app_host_gatt_handle_for_type(app_host_frame_type_t type)
{
    if (type == HOST_FRAME_TYPE_LOG) {
        return ATT_HANDLE_DEBUG_LOG_VALUE;
    }
    if (type == HOST_FRAME_TYPE_EVENT) {
        return ATT_HANDLE_DEBUG_EVT_VALUE;
    }
    return ATT_HANDLE_DEBUG_RSP_VALUE;
}

static app_status_t app_host_gatt_queue_notify(u16 handle, const u8 *data, u8 len)
{
    host_notify_item_t *item;

    if (!data || !len || len > APP_HOST_FRAME_MAX_PACKET_LEN) {
        return APP_ERR_PARAM;
    }
    if (s_q_count >= HOST_GATT_NOTIFY_QUEUE_SIZE) {
        return APP_ERR_NO_MEM;
    }

    item = &s_notify_queue[s_q_tail];
    item->handle = handle;
    item->len = len;
    memcpy(item->data, data, len);
    s_q_tail = (u8)((s_q_tail + 1) & (HOST_GATT_NOTIFY_QUEUE_SIZE - 1));
    s_q_count++;
    return APP_OK;
}

static int app_host_gatt_cmd_write_cb(u16 conn_handle, void *p)
{
    rf_packet_att_t *pkt = (rf_packet_att_t *)p;
    u16 len;

    (void)conn_handle;
    if (!pkt || pkt->l2capLen < 3) {
        return 0;
    }

    len = (u16)(pkt->l2capLen - 3);
    if (len > APP_HOST_FRAME_MAX_PACKET_LEN) {
        len = APP_HOST_FRAME_MAX_PACKET_LEN;
    }
    app_host_cmd_on_rx_frame((const u8 *)pkt->dat, (u8)len);
    return 0;
}

static int app_host_gatt_ccc_write_cb(u16 conn_handle, void *p)
{
    rf_packet_att_t *pkt = (rf_packet_att_t *)p;
    u16 value;

    (void)conn_handle;
    if (!pkt || pkt->l2capLen < 5) {
        return 0;
    }

    value = (u16)pkt->dat[0] | ((u16)pkt->dat[1] << 8);
    if (pkt->handle == ATT_HANDLE_DEBUG_RSP_CCC) {
        s_rsp_ccc = value;
    } else if (pkt->handle == ATT_HANDLE_DEBUG_LOG_CCC) {
        s_log_ccc = value;
    } else if (pkt->handle == ATT_HANDLE_DEBUG_EVT_CCC) {
        s_evt_ccc = value;
    }
    return 0;
}

void app_host_gatt_init(void)
{
    s_rsp_ccc = 0;
    s_log_ccc = 0;
    s_evt_ccc = 0;
    s_q_head = 0;
    s_q_tail = 0;
    s_q_count = 0;
    s_connected = 0;
    s_conn_handle = 0;

    bls_att_setAttributeTable((u8 *)s_att_table);
    blc_smp_setSecurityLevel(No_Security);
}

void app_host_gatt_poll(void)
{
    host_notify_item_t *item;
    ble_sts_t st;

    if (!s_connected || !s_q_count) {
        return;
    }

    item = &s_notify_queue[s_q_head];
    if (!app_host_gatt_notify_enabled(item->handle)) {
        s_q_head = (u8)((s_q_head + 1) & (HOST_GATT_NOTIFY_QUEUE_SIZE - 1));
        s_q_count--;
        return;
    }

    st = blc_gatt_pushHandleValueNotify(s_conn_handle, item->handle, item->data, item->len);
    if (st == BLE_SUCCESS) {
        s_q_head = (u8)((s_q_head + 1) & (HOST_GATT_NOTIFY_QUEUE_SIZE - 1));
        s_q_count--;
    }
}

void app_host_gatt_on_connected(u16 conn_handle)
{
    s_connected = 1;
    s_conn_handle = conn_handle;
}

void app_host_gatt_on_disconnected(void)
{
    s_connected = 0;
    s_rsp_ccc = 0;
    s_log_ccc = 0;
    s_evt_ccc = 0;
    s_q_head = 0;
    s_q_tail = 0;
    s_q_count = 0;
    s_conn_handle = 0;
}

u8 app_host_gatt_is_ready(void)
{
    return s_connected;
}

app_status_t app_host_gatt_send_message_with_seq(app_host_frame_type_t type, u8 seq, u8 cmd, u8 status, const u8 *payload, u16 len)
{
    app_host_frame_tx_t frame;
    u8 packet[APP_HOST_FRAME_MAX_PACKET_LEN];
    u8 packet_len;
    u8 frag_count;
    u8 i;
    u16 offset = 0;
    u16 handle;

    if (len && !payload) {
        return APP_ERR_PARAM;
    }
    if (len > APP_HOST_MESSAGE_MAX_LEN) {
        len = APP_HOST_MESSAGE_MAX_LEN;
    }

    handle = app_host_gatt_handle_for_type(type);
    if (!s_connected || !app_host_gatt_notify_enabled(handle)) {
        return APP_ERR_STATE;
    }

    frag_count = (u8)((len + APP_HOST_FRAME_CHUNK_MAX_LEN - 1) / APP_HOST_FRAME_CHUNK_MAX_LEN);
    if (!frag_count) {
        frag_count = 1;
    }
    if (s_q_count + frag_count > HOST_GATT_NOTIFY_QUEUE_SIZE) {
        return APP_ERR_NO_MEM;
    }

    for (i = 0; i < frag_count; i++) {
        u8 chunk_len = (u8)((len - offset) > APP_HOST_FRAME_CHUNK_MAX_LEN ? APP_HOST_FRAME_CHUNK_MAX_LEN : (len - offset));
        frame.type = type;
        frame.seq = seq;
        frame.cmd = cmd;
        frame.status = status;
        frame.frag_index = i;
        frame.frag_count = frag_count;
        frame.payload = chunk_len ? &payload[offset] : 0;
        frame.payload_len = chunk_len;
        if (app_host_frame_encode(&frame, packet, sizeof(packet), &packet_len) != APP_OK) {
            return APP_ERR_PARAM;
        }
        if (app_host_gatt_queue_notify(handle, packet, packet_len) != APP_OK) {
            return APP_ERR_NO_MEM;
        }
        offset = (u16)(offset + chunk_len);
    }

    return APP_OK;
}

app_status_t app_host_gatt_send_message(app_host_frame_type_t type, u8 cmd, u8 status, const u8 *payload, u16 len)
{
    return app_host_gatt_send_message_with_seq(type, app_host_cmd_next_tx_seq(), cmd, status, payload, len);
}
