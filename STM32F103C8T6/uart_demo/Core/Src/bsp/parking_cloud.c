#include "parking_cloud.h"

#include "parking_db.h"
#include "usart.h"

#include <stdio.h>
#include <string.h>

#define CLOUD_STATUS_INTERVAL_MS 3000u
#define CLOUD_RX_LINE_MAX 96
#define CLOUD_RESERVED_CACHE_MAX 8
#define CLOUD_RESERVED_SYNC_TTL_MS 15000u

static volatile uint32_t *g_ms = 0;
static uint32_t g_last_status_ms = 0;
static int g_last_normal_capacity = -1;
static int g_last_normal_active = -1;
static int g_last_reserved_capacity = -1;
static int g_last_reserved_active = -1;
static uint8_t g_status_dirty = 1;

static char g_rx_line[CLOUD_RX_LINE_MAX];
static uint16_t g_rx_line_len = 0;
static char g_rx_type[24];
static char g_rx_plates_csv[CLOUD_RX_LINE_MAX];

static char g_reserved_plates[CLOUD_RESERVED_CACHE_MAX][DB_PLATE_MAX];
static uint8_t g_reserved_count = 0;
static uint32_t g_reserved_sync_ms = 0;

static void uart3_send_text(const char *text)
{
    if (!text) return;
    HAL_UART_Transmit(&huart3, (uint8_t *)text, (uint16_t)strlen(text), 200);
}

static void send_line_kv_str(const char *key, const char *value)
{
    char line[96];
    snprintf(line, sizeof(line), "%s=%s\n", key, value ? value : "");
    uart3_send_text(line);
}

static void send_line_kv_u32(const char *key, uint32_t value)
{
    char line[64];
    snprintf(line, sizeof(line), "%s=%lu\n", key, (unsigned long)value);
    uart3_send_text(line);
}

static void send_end(void)
{
    uart3_send_text("END\n");
}

static const char *slot_type_text(db_slot_type_t slot_type)
{
    return (slot_type == DB_SLOT_RESERVED) ? "RESERVED" : "NORMAL";
}

static int plate_eq(const char *a, const char *b)
{
    if (!a || !b) return 0;
    return strncmp(a, b, DB_PLATE_MAX) == 0;
}

static uint8_t reservation_cache_fresh(void)
{
    uint32_t now = g_ms ? *g_ms : 0;
    if (g_reserved_sync_ms == 0) return 0;
    return (uint32_t)(now - g_reserved_sync_ms) <= CLOUD_RESERVED_SYNC_TTL_MS;
}

static void reservation_cache_clear(void)
{
    memset(g_reserved_plates, 0, sizeof(g_reserved_plates));
    g_reserved_count = 0;
}

static void reservation_cache_add(const char *plate)
{
    if (!plate || plate[0] == '\0') return;

    for (uint8_t i = 0; i < g_reserved_count; ++i)
    {
        if (plate_eq(g_reserved_plates[i], plate))
            return;
    }

    if (g_reserved_count >= CLOUD_RESERVED_CACHE_MAX)
        return;

    strncpy(g_reserved_plates[g_reserved_count], plate, DB_PLATE_MAX - 1);
    g_reserved_plates[g_reserved_count][DB_PLATE_MAX - 1] = '\0';
    g_reserved_count++;
}

static void reservation_cache_load_csv(const char *csv)
{
    reservation_cache_clear();

    if (csv && csv[0] != '\0')
    {
        char buffer[CLOUD_RX_LINE_MAX];
        strncpy(buffer, csv, sizeof(buffer) - 1);
        buffer[sizeof(buffer) - 1] = '\0';

        char *token = strtok(buffer, ",");
        while (token)
        {
            while (*token == ' ') token++;
            reservation_cache_add(token);
            token = strtok(0, ",");
        }
    }

    g_reserved_sync_ms = g_ms ? *g_ms : 0;
}

static void reset_rx_frame(void)
{
    g_rx_type[0] = '\0';
    g_rx_plates_csv[0] = '\0';
}

static void finalize_rx_frame(void)
{
    if (strcmp(g_rx_type, "RESERVATION_SYNC") == 0)
        reservation_cache_load_csv(g_rx_plates_csv);

    reset_rx_frame();
}

static void parse_rx_line(const char *line)
{
    if (strcmp(line, "END") == 0)
    {
        finalize_rx_frame();
        return;
    }

    const char *sep = strchr(line, '=');
    if (!sep) return;

    const size_t key_len = (size_t)(sep - line);
    const char *value = sep + 1;

    if (key_len == 4 && strncmp(line, "TYPE", 4) == 0)
    {
        strncpy(g_rx_type, value, sizeof(g_rx_type) - 1);
        g_rx_type[sizeof(g_rx_type) - 1] = '\0';
    }
    else if (key_len == 6 && strncmp(line, "PLATES", 6) == 0)
    {
        strncpy(g_rx_plates_csv, value, sizeof(g_rx_plates_csv) - 1);
        g_rx_plates_csv[sizeof(g_rx_plates_csv) - 1] = '\0';
    }
}

static void send_status_frame(int normal_capacity, int normal_active, int reserved_capacity, int reserved_active)
{
    const int total_capacity = normal_capacity + reserved_capacity;
    const int total_active = normal_active + reserved_active;
    int total_free = total_capacity - total_active;
    int normal_free = normal_capacity - normal_active;
    int reserved_free = reserved_capacity - reserved_active;

    if (total_free < 0) total_free = 0;
    if (normal_free < 0) normal_free = 0;
    if (reserved_free < 0) reserved_free = 0;

    send_line_kv_str("TYPE", "STATUS");
    send_line_kv_u32("TOTAL", (uint32_t)total_capacity);
    send_line_kv_u32("ACTIVE", (uint32_t)total_active);
    send_line_kv_u32("FREE", (uint32_t)total_free);
    send_line_kv_u32("NORMAL_TOTAL", (uint32_t)normal_capacity);
    send_line_kv_u32("NORMAL_ACTIVE", (uint32_t)normal_active);
    send_line_kv_u32("NORMAL_FREE", (uint32_t)normal_free);
    send_line_kv_u32("RESERVED_TOTAL", (uint32_t)reserved_capacity);
    send_line_kv_u32("RESERVED_ACTIVE", (uint32_t)reserved_active);
    send_line_kv_u32("RESERVED_FREE", (uint32_t)reserved_free);
    send_end();
}

void parking_cloud_init(volatile uint32_t *ms_tick_ptr)
{
    g_ms = ms_tick_ptr;
    g_last_status_ms = 0;
    g_last_normal_capacity = -1;
    g_last_normal_active = -1;
    g_last_reserved_capacity = -1;
    g_last_reserved_active = -1;
    g_status_dirty = 1;
    g_rx_line_len = 0;
    reset_rx_frame();
    reservation_cache_clear();
    g_reserved_sync_ms = 0;
}

void parking_cloud_force_status(void)
{
    g_status_dirty = 1;
}

void parking_cloud_on_rx_byte(uint8_t byte)
{
    if (byte == '\r')
        return;

    if (byte == '\n')
    {
        g_rx_line[g_rx_line_len] = '\0';
        if (g_rx_line_len > 0)
            parse_rx_line(g_rx_line);
        g_rx_line_len = 0;
        return;
    }

    if (g_rx_line_len + 1 < CLOUD_RX_LINE_MAX)
    {
        g_rx_line[g_rx_line_len++] = (char)byte;
    }
    else
    {
        g_rx_line_len = 0;
    }
}

parking_cloud_res_state_t parking_cloud_lookup_reserved_plate(const char *plate)
{
    for (uint8_t i = 0; i < g_reserved_count; ++i)
    {
        if (plate_eq(g_reserved_plates[i], plate))
            return reservation_cache_fresh() ? PARKING_CLOUD_RES_ACTIVE : PARKING_CLOUD_RES_STALE;
    }

    return PARKING_CLOUD_RES_NONE;
}

void parking_cloud_consume_reserved_plate(const char *plate)
{
    for (uint8_t i = 0; i < g_reserved_count; ++i)
    {
        if (!plate_eq(g_reserved_plates[i], plate))
            continue;

        for (uint8_t j = i; j + 1 < g_reserved_count; ++j)
            memmove(g_reserved_plates[j], g_reserved_plates[j + 1], DB_PLATE_MAX);

        memset(g_reserved_plates[g_reserved_count - 1], 0, DB_PLATE_MAX);
        g_reserved_count--;
        return;
    }
}

void parking_cloud_poll(void)
{
    uint32_t now = g_ms ? *g_ms : 0;
    int normal_capacity = db_capacity_by_type(DB_SLOT_NORMAL);
    int normal_active = db_count_active_by_type(DB_SLOT_NORMAL);
    int reserved_capacity = db_capacity_by_type(DB_SLOT_RESERVED);
    int reserved_active = db_count_active_by_type(DB_SLOT_RESERVED);
    uint8_t changed = (normal_capacity != g_last_normal_capacity)
        || (normal_active != g_last_normal_active)
        || (reserved_capacity != g_last_reserved_capacity)
        || (reserved_active != g_last_reserved_active);

    if (changed)
    {
        g_last_normal_capacity = normal_capacity;
        g_last_normal_active = normal_active;
        g_last_reserved_capacity = reserved_capacity;
        g_last_reserved_active = reserved_active;
        g_status_dirty = 1;
    }

    if (!g_status_dirty && (now - g_last_status_ms < CLOUD_STATUS_INTERVAL_MS))
        return;

    send_status_frame(normal_capacity, normal_active, reserved_capacity, reserved_active);
    g_last_status_ms = now;
    g_status_dirty = 0;
}

void parking_cloud_publish_enter(const char *plate, uint32_t in_ms, db_slot_type_t slot_type)
{
    send_line_kv_str("TYPE", "EVENT");
    send_line_kv_str("EV", "IN");
    send_line_kv_str("PLATE", plate);
    send_line_kv_str("MODE", slot_type_text(slot_type));
    send_line_kv_u32("IN_TS", in_ms);
    send_end();
    parking_cloud_force_status();
}

void parking_cloud_publish_exit(const char *plate, uint32_t in_ms, uint32_t out_ms, uint32_t fee_cents, db_slot_type_t slot_type)
{
    send_line_kv_str("TYPE", "EVENT");
    send_line_kv_str("EV", "OUT");
    send_line_kv_str("PLATE", plate);
    send_line_kv_str("MODE", slot_type_text(slot_type));
    send_line_kv_u32("IN_TS", in_ms);
    send_line_kv_u32("OUT_TS", out_ms);
    send_line_kv_u32("FEE", fee_cents);
    send_end();
    parking_cloud_force_status();
}
