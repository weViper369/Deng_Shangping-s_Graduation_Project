#ifndef __MAIX_LINK_H
#define __MAIX_LINK_H
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  Maix 串口链路运行统计信息
 *
 * 该结构体用于监控串口通信健康状态，
 * 可用于调试、日志输出、异常分析。
 *
 * 所有计数器均为累计计数（上电后递增）。
 * @param 
 * uint32_t bytes_in;
 * 已接收到的总字节数（包含帧头、payload、CRC）
 * 用于判断串口是否在持续收数据
 * uint32_t frames_ok;
 * 成功解析且 CRC 校验通过的帧数量
 * 这是判断通信是否正常的核心指标
 * uint32_t frames_bad_crc;
 * CRC 校验失败的帧数量
 * 如果该值持续增加，说明：
 *  - Cam 端 CRC 算法不一致
 *  - LEN 字段错误
 *  - 串口干扰导致数据损坏
 * uint32_t frames_drop; 
 * 因帧格式错误被丢弃的帧数量
 * 例如：
 *  - 未找到 AA55
 *  - LEN 超过最大允许长度
 *  - 半包超时
 * uint32_t rb_overflow;
 * 环形缓冲区溢出次数
 * 如果该值不为 0，说明：
 *  - 主循环处理不及时
 *  - 打印太多阻塞
 *  - 串口速率过高
 */
typedef struct {
    uint32_t bytes_in;      
    uint32_t frames_ok;     
    uint32_t frames_bad_crc;
    uint32_t frames_drop;   
    uint32_t rb_overflow;   
} maix_link_stats_t;
/**
 * @brief  车牌识别结果事件
 *
 * 该结构体表示从 Maix 识别模块接收到的一条
 * 有效车牌数据。
 *
 * 注意：
 * 该结构体属于“事件模型”，
 * 由 maix_link 模块产生，
 * 由上层 FSM 消费。
 * @param 
 * uint8_t conf;  
 * 识别置信度（0~100）
 * 由 Cam 端传输，用于后续判断是否放行
 * 例如：低于 60 可以拒绝开闸
 * char plate[32];
 * 车牌字符串（UTF-8 编码）
 * 最大支持 31 字节 + '\0'
 * 
 * 示例：
 * "京N8P8F8"
 * 实际存储为：
 * E4 BA AC 4E 38 50 38 46 38 00
 * 
 * 注意：
 * - 中文占 3 字节
 * - 不保证终端一定能正确显示（取决于编码）
 * uint8_t valid;
 * 事件有效标志
 * 1 = 有新车牌事件（尚未被 FSM 消费）
 * 0 = 无事件
 * maix_link_get_plate() 取走后会自动清零
 * 该字段用于防止重复消费
 * uint8_t lane;
 */
typedef struct {

    uint8_t conf;           
    char plate[33];         
    uint8_t valid;   
    uint8_t lane;       
} maix_plate_event_t;

typedef enum{
    EXP_NONE = 0,
    EXP_IN = 1,
    EXP_OUT = 2,
}exp_lane_t;

typedef struct {
    uint8_t  pending;        // 1=正在等待该路结果
    uint32_t expected_t0;    // 本路等待起始时间
    uint32_t last_req_ms;    // 本路上次REQ时间（重试用）
} ocr_slot_t;

typedef struct
{
    ocr_slot_t in;
    ocr_slot_t out;

    uint8_t    in_latch;        // 入口防重复触发
    uint8_t    out_latch;       // 出口防重复触发
    uint32_t   req_timeout_ms;

    uint8_t in_pending;     // IN 是否还在等待识别结果
    uint8_t out_pending;    // OUT 是否还在等待识别结果

    uint32_t retry_interval_ms; // blocked期间重试间隔，比如 4500
} ocr_ctrl_t;

// 给 CAM 发送请求接口
void maix_link_send_req_ocr(uint8_t lane);

// 初始化：传入当前毫秒计数
void maix_link_init(volatile uint32_t *ms_tick_ptr);

// poll：主循环里反复调用
void maix_link_poll(void);

// 给 RX 中断回调喂一个字节
void maix_link_on_rx_byte(uint8_t b);

// 取事件：返回1表示取到一个新车牌事件（取完自动清 valid）
int  maix_link_get_plate(maix_plate_event_t *out);

// 取统计
void maix_link_get_stats(maix_link_stats_t *out);

// 重置统计
void maix_link_reset_stats(void);

#ifdef __cplusplus
}
#endif 
#endif 
