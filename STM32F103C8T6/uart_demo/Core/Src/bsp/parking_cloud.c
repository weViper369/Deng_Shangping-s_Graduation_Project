#include "parking_cloud.h"

#include "parking_db.h"
#include "usart.h"

#include <stdio.h>
#include <string.h>

#define CLOUD_STATUS_INTERVAL_MS 3000u

static volatile uint32_t *g_ms = 0;
static uint32_t g_last_status_ms = 0;
static int g_last_capacity = -1;
static int g_last_active = -1;
static uint8_t g_status_dirty = 1;

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

static void send_status_frame(int capacity, int active)
{
    int free_slots = capacity - active;

    if (free_slots < 0) free_slots = 0;

    send_line_kv_str("TYPE", "STATUS");
    send_line_kv_u32("TOTAL", (uint32_t)capacity);
    send_line_kv_u32("ACTIVE", (uint32_t)active);
    send_line_kv_u32("FREE", (uint32_t)free_slots);
    send_end();
}

void parking_cloud_init(volatile uint32_t *ms_tick_ptr)
{
    g_ms = ms_tick_ptr;
    g_last_status_ms = 0;
    g_last_capacity = -1;
    g_last_active = -1;
    g_status_dirty = 1;
}

void parking_cloud_force_status(void)
{
    g_status_dirty = 1;
}

void parking_cloud_poll(void)
{
    uint32_t now = g_ms ? *g_ms : 0;
    int capacity = db_capacity();
    int active = db_count_active();
    uint8_t changed = (capacity != g_last_capacity) || (active != g_last_active);

    if (changed)
    {
        g_last_capacity = capacity;
        g_last_active = active;
        g_status_dirty = 1;
    }

    if (!g_status_dirty && (now - g_last_status_ms < CLOUD_STATUS_INTERVAL_MS))
        return;

    send_status_frame(capacity, active);
    g_last_status_ms = now;
    g_status_dirty = 0;
}

void parking_cloud_publish_enter(const char *plate, uint32_t in_ms)
{
    send_line_kv_str("TYPE", "EVENT");
    send_line_kv_str("EV", "IN");
    send_line_kv_str("PLATE", plate);
    send_line_kv_u32("IN_TS", in_ms);
    send_end();
    parking_cloud_force_status();
}

void parking_cloud_publish_exit(const char *plate, uint32_t in_ms, uint32_t out_ms, uint32_t fee_cents)
{
    send_line_kv_str("TYPE", "EVENT");
    send_line_kv_str("EV", "OUT");
    send_line_kv_str("PLATE", plate);
    send_line_kv_u32("IN_TS", in_ms);
    send_line_kv_u32("OUT_TS", out_ms);
    send_line_kv_u32("FEE", fee_cents);
    send_end();
    parking_cloud_force_status();
}
