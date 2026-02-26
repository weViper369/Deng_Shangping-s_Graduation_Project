#include "ui.h"
#include "ssd1306.h"
#include "parking_db.h"
#include <stdio.h>
#include <string.h>

typedef enum { 
    UI_HOME=0,
    UI_IN_EVT=1, 
    UI_OUT_EVT=2,  // 废弃了  
    UI_PAY_WAIT=3, 
    UI_PAY_OK=4,   
    UI_ERR=5,
} ui_page_t;

static ui_page_t page = UI_HOME;
static uint32_t  page_until_ms = 0;

static char      evt_plate[33];
static uint32_t  evt_dur_s = 0;
static uint32_t  evt_fee = 0;

static char err_msg[20] = {0};  // 错误原因短语（ASCII/你固定中文ID）
static char err_plate[33] = {0}; // 关联车牌

static void ui_set_page(ui_page_t p, uint32_t now, uint32_t hold_ms)
{
    page = p;
    page_until_ms = (hold_ms == 0) ? 0 : (now + hold_ms);
}

void ui_init(void)
{
    ssd1306_init();
    page = UI_HOME;
    page_until_ms = 0;
    evt_plate[0] = 0;
    evt_dur_s = 0;
    evt_fee = 0;
}

void ui_on_in_plate(const char *plate, uint32_t now_ms)
{
    strncpy(evt_plate, plate ? plate : "", sizeof(evt_plate)-1);
    evt_plate[sizeof(evt_plate)-1] = 0;

    // page = UI_IN_EVT;
    // // page_until_ms 在 ui_tick(now) 里用 now 设置也行，这里先置 0
    // page_until_ms = 0;
    ui_set_page(UI_IN_EVT, now_ms, 4000);  // 显示4秒
}

// 废弃了
void ui_on_out_bill(const char *plate, uint32_t dur_s, uint32_t fee_cents, uint32_t now_ms)
{
    strncpy(evt_plate, plate ? plate : "", sizeof(evt_plate)-1);
    evt_plate[sizeof(evt_plate)-1] = 0;
    evt_dur_s = dur_s;
    evt_fee = fee_cents;

    // page = UI_OUT_EVT;
    // page_until_ms = 0;
    ui_set_page(UI_OUT_EVT, now_ms, 4000);
}

void ui_on_pay_wait(const char *plate, uint32_t dur_s, uint32_t fee_cents, uint32_t now_ms)
{
    strncpy(evt_plate, plate ? plate : "", sizeof(evt_plate)-1);
    evt_plate[sizeof(evt_plate)-1] = 0;
    evt_dur_s = dur_s;
    evt_fee = fee_cents;

    ui_set_page(UI_PAY_WAIT, now_ms, 0); // 0表示不自动回HOME（一直等）
}

void ui_on_pay_ok(const char *plate, uint32_t fee_cents, uint32_t now_ms)
{
    strncpy(evt_plate, plate ? plate : "", sizeof(evt_plate)-1);
    evt_plate[sizeof(evt_plate)-1] = 0;
    evt_fee = fee_cents;

    ui_set_page(UI_PAY_OK, now_ms, 4000); // 显示4秒后回HOME
}
void ui_on_error(const char *msg, const char *plate, uint32_t now_ms)
{
    strncpy(err_msg, msg ? msg : "", sizeof(err_msg)-1);
    err_msg[sizeof(err_msg)-1] = 0;

    strncpy(err_plate, plate ? plate : "", sizeof(err_plate)-1);
    err_plate[sizeof(err_plate)-1] = 0;

    ui_set_page(UI_ERR, now_ms, 4000); // 错误页显示 4 秒
}


static void draw_home(uint32_t now_ms)
{
    char l1[32], l2[32], l3[32];
    static const uint16_t CN_ZAICHANGCHELIANG[] = {0x5728, 0x573A, 0x8F66, 0x8F86}; // 在 场 车 辆
    static const uint16_t CN_SHENGYUCHEWEI[] = {0x5269, 0x4F59, 0x8F66, 0x4F4D};// 剩 余 车 位
    int active = db_count_active();
    int cap = db_capacity();
    int left = cap - active;
    if (left < 0) left = 0;

    snprintf(l1, sizeof(l1), "PARKING SYSTEM");
    snprintf(l2, sizeof(l2), ": %d", active);
    snprintf(l3, sizeof(l3), ": %d", left);

    ssd1306_draw_str(0, 0,  l1);
    oled_draw_cn16(0, 16, CN_ZAICHANGCHELIANG, 4);
    ssd1306_draw_str(65, 16, l2);
    oled_draw_cn16(0, 32, CN_SHENGYUCHEWEI, 4);
    ssd1306_draw_str(65, 32, l3);
}

static void draw_in_evt(void)
{
    char l2[32];
    static const uint16_t CN_IN_OK[] = { 0x5165,0x573A,0x6210,0x529F }; // 入 场 成 功
    static const uint16_t CN_PLATE[] = { 0x8F66,0x724C }; // 车 牌
    static const uint16_t CN_QINGTONGXING[] = { 0x8BF7,0x901A,0x884C }; // 请 通 行

    // plate_to_oled_ascii(evt_plate, pbuf, sizeof(pbuf));
    snprintf(l2, sizeof(l2), ":");

    oled_draw_cn16(0, 0, CN_IN_OK, 4);

    oled_draw_cn16(0, 16, CN_PLATE, 2);
    ssd1306_draw_str(32, 16, l2);
    draw_plate_utf8(40, 16, evt_plate);

    oled_draw_cn16(0, 32, CN_QINGTONGXING, 3);
}

static void draw_out_evt(void)
{
    static const uint16_t CN_PAYOK[]   = { 0x652F,0x4ED8,0x6210,0x529F }; // 支 付 成 功
    static const uint16_t CN_LEAVE[] = { 0x8BF7,0x79BB,0x573A }; // 请 离 场
    static const uint16_t CN_PLATE[] = { 0x8F66, 0x724C }; // 车 牌

    // 第1行：支付成功
    oled_draw_cn16(0, 0, CN_PAYOK, 4);
    // 第2行：请离场（如果你更想显示时长，把这一行换成时长即可）
    oled_draw_cn16(0, 16, CN_LEAVE, 3);
    // 第3行：车牌
    oled_draw_cn16(0, 32, CN_PLATE, 2);
    ssd1306_draw_str(32, 32, ":");
    draw_plate_utf8(40, 32, evt_plate);
}

static void draw_pay_wait(void)
{
    char money[16];
    uint32_t yuan = evt_fee / 100;
    uint32_t cent = evt_fee % 100;

    static const uint16_t CN_WAITPAY[] = { 0x7B49,0x5F85,0x652F,0x4ED8 }; // 等待支付
    static const uint16_t CN_PLATE[]   = { 0x8F66,0x724C };             // 车牌
    static const uint16_t CN_PAY[]     = { 0x5E94,0x4ED8 };             // 应付
    static const uint16_t CN_YUAN[]    = { 0x5143 };                    // 元

    oled_draw_cn16(0, 0, CN_WAITPAY, 4);

    oled_draw_cn16(0, 16, CN_PLATE, 2);
    ssd1306_draw_str(32, 16, ":");
    draw_plate_utf8(40, 16, evt_plate);

    oled_draw_cn16(0, 32, CN_PAY, 2);
    ssd1306_draw_str(32, 32, ":");
    snprintf(money, sizeof(money), "%lu.%02lu", (unsigned long)yuan, (unsigned long)cent);
    ssd1306_draw_str(40, 32, money);
    oled_draw_cn16(96, 32, CN_YUAN, 1);

}

static void draw_pay_ok(void)
{
    char money[16];
    uint32_t yuan = evt_fee / 100;
    uint32_t cent = evt_fee % 100;

    static const uint16_t CN_OK[]    = { 0x652F,0x4ED8,0x6210,0x529F }; // 支付成功
    static const uint16_t CN_PLATE[] = { 0x8F66,0x724C };              // 车牌
    static const uint16_t CN_RCVD[]  = { 0x5DF2,0x6536 };              // 已收
    static const uint16_t CN_YUAN[]  = { 0x5143 };                     // 元

    oled_draw_cn16(0, 0, CN_OK, 4);

    oled_draw_cn16(0, 16, CN_PLATE, 2);
    ssd1306_draw_str(32, 16, ":");
    draw_plate_utf8(40, 16, evt_plate);

    oled_draw_cn16(0, 32, CN_RCVD, 2);
    ssd1306_draw_str(32, 32, ":");
    snprintf(money, sizeof(money), "%lu.%02lu", (unsigned long)yuan, (unsigned long)cent);
    ssd1306_draw_str(40, 32, money);
    oled_draw_cn16(96, 32, CN_YUAN, 1);
}

static void draw_out_pay_evt(void)
{
    char l3[32], l4[32];
    static const uint16_t CN_OUT_BILL[] = { 0x51FA, 0x573A, 0x7ED3, 0x7B97 }; // 出 场 结 算
    static const uint16_t CN_PLATE[] = { 0x8F66, 0x724C }; // 车 牌
    static const uint16_t CN_FEE[] = { 0x8D39, 0x7528 }; // 费 用
    static const uint16_t CN_DUR[] = { 0x65F6, 0x957F }; // 时 长
    static const uint16_t YUAN[] = {0x5143}; // 元
    uint16_t yuan = evt_fee / 100;
    uint16_t cent = evt_fee % 100;
    // plate_to_oled_ascii(evt_plate, pbuf, sizeof(pbuf));
    snprintf(l3, sizeof(l3), "%lu.%02lu", (unsigned long)yuan,(unsigned long)cent);
    snprintf(l4, sizeof(l4), ":%lus", (unsigned long)evt_dur_s);

    oled_draw_cn16(0, 0, CN_OUT_BILL, 4);

    oled_draw_cn16(0, 16, CN_PLATE, 2);
    ssd1306_draw_str(32, 16, ":");
    draw_plate_utf8(40, 16, evt_plate); 

    oled_draw_cn16(0, 32, CN_FEE, 2);
    ssd1306_draw_str(32, 32, ":");
    ssd1306_draw_str(40, 32, l3);
    oled_draw_cn16(88, 32, YUAN, 1);

    oled_draw_cn16(0, 48, CN_DUR, 2);
    ssd1306_draw_str(32, 48, l4);
}

static void draw_err(void)
{
    static const uint16_t CN_ERR_TITLE[] = { 0x9519, 0x8BEF, 0x63D0, 0x793A }; // 错 误 提 示
    static const uint16_t CN_PLATE[]     = { 0x8F66, 0x724C };               // 车 牌

    // 错误原因短语
    static const uint16_t CN_DUP[]       = { 0x91CD,0x590D,0x5165,0x573A }; // 重复入场
    static const uint16_t CN_FULL[]      = { 0x8F66,0x4F4D,0x5DF2,0x6EE1 }; // 车位已满
    static const uint16_t CN_NOTFOUND[]  = { 0x672A,0x627E,0x5230,0x8BB0,0x5F55 }; // 未找到记录(5字)
    static const uint16_t CN_OCR_TO[]    = { 0x8BC6,0x522B,0x8D85,0x65F6 }; // 识别超时

    // 第1行：错误提示
    oled_draw_cn16(0, 0, CN_ERR_TITLE, 4);

    // 第2行：原因（先用ASCII，省字模）
    if(!strcmp(err_msg, "DUP"))        
        oled_draw_cn16(0, 16, CN_DUP, 4);
    else if (!strcmp(err_msg, "FULL"))       
        oled_draw_cn16(0, 16, CN_FULL, 4);
    else if (!strcmp(err_msg, "NOTFOUND"))   
        oled_draw_cn16(0, 16, CN_NOTFOUND, 5);
    // else if (!strcmp(err_msg, "OCR TIMEOUT"))
        // oled_draw_cn16(0, 16, CN_OCR_TO, 4);
    // else                                     
        // oled_draw_cn16(0, 16, CN_UNKNOWN, 4 );   
    // ssd1306_draw_str(0, 16, err_msg);

    // 第3行：车牌: + UTF-8 混排（如果有）
    if (err_plate[0])
    {
        oled_draw_cn16(0, 32, CN_PLATE, 2);
        ssd1306_draw_str(32, 32, ":");
        draw_plate_utf8(40, 32, err_plate);
    }

    // 第4行：可选提示
    // ssd1306_draw_str(0, 48, "PLEASE RETRY");
}


void ui_tick(uint32_t now_ms)
{
    // 第一次进入事件页时，设置“显示到什么时候”
    // if ((page == UI_IN_EVT || page == UI_OUT_EVT) && page_until_ms == 0)
        // page_until_ms = now_ms + 4000; // 事件页显示 4 秒

    // 到时间自动回主页
    if (page != UI_HOME && page_until_ms != 0  && now_ms >= page_until_ms)
    {
        ui_set_page(UI_HOME, now_ms, 0);
    }

    ssd1306_fill(0);

    switch(page)
    {
        case UI_HOME:
            draw_home(now_ms);
            break;
        case UI_IN_EVT:
            draw_in_evt();
            break;
        case UI_PAY_WAIT:
            draw_pay_wait();
            break;
        case UI_OUT_EVT:
            draw_out_evt();
            break;
        case UI_PAY_OK:
            draw_pay_ok();
            break;
        case UI_ERR:
            draw_err();
            break;
    }
    // if (page == UI_HOME)      
    //     draw_home(now_ms);
    // else if (page == UI_IN_EVT)  
    //     draw_in_evt();
    // else if (page == UI_OUT_EVT)                       
    //     draw_out_evt();

    ssd1306_update();
}

