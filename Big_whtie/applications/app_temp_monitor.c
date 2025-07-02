#include <rtthread.h>
#include <rtdevice.h>
#include <board.h>
#include "aht10.h"

/* 硬件配置 */
#define PIN_LED_R      GET_PIN(F, 12)   // 红色LED引脚
#define PIN_LED_Y      GET_PIN(F, 11)   // 黄色LED引脚
#define PIN_BEEP       GET_PIN(B, 0)    // 蜂鸣器引脚
#define TEMP_MIN       0.0f             // 最低温度阈值(℃)
#define TEMP_MAX       40.0f            // 最高温度阈值(℃)
#define HUMI_MIN       10.0f            // 最低湿度阈值(%)
#define HUMI_MAX       90.0f            // 最高湿度阈值(%)
#define BLINK_INTERVAL 500              // 闪烁间隔(ms)
#define SAMPLE_INTERVAL 1000            // 采样间隔(ms)

/* 报警状态结构体 */
struct alert_status {
    rt_bool_t temp_alert;
    rt_bool_t humi_alert;
    rt_bool_t beep_state;
    rt_uint32_t last_blink_time;
};

static aht10_device_t dev = RT_NULL;

/* 初始化硬件 */
static void hardware_init(void)
{
    rt_pin_mode(PIN_LED_R, PIN_MODE_OUTPUT);
    rt_pin_write(PIN_LED_R, PIN_HIGH);
    rt_pin_mode(PIN_LED_Y, PIN_MODE_OUTPUT);
    rt_pin_write(PIN_LED_Y, PIN_HIGH);
    rt_pin_mode(PIN_BEEP, PIN_MODE_OUTPUT);
    rt_pin_write(PIN_BEEP, PIN_LOW);
}

/* 更新报警状态 */
static void update_alert_status(struct alert_status *status, float temp, float humi)
{
    rt_uint32_t current_time = rt_tick_get();
    rt_bool_t blink_state = (current_time - status->last_blink_time) >=
                          rt_tick_from_millisecond(BLINK_INTERVAL);

    /* 更新报警状态 */
    status->temp_alert = (temp < TEMP_MIN || temp > TEMP_MAX);
    status->humi_alert = (humi < HUMI_MIN || humi > HUMI_MAX);

    /* 处理灯光状态 */
    if (blink_state) {
        status->last_blink_time = current_time;

        if (status->temp_alert && !status->humi_alert) {
            rt_pin_write(PIN_LED_R, !rt_pin_read(PIN_LED_R));
            rt_pin_write(PIN_LED_Y, PIN_HIGH);
        }
        else if (!status->temp_alert && status->humi_alert) {
            rt_pin_write(PIN_LED_Y, !rt_pin_read(PIN_LED_Y));
            rt_pin_write(PIN_LED_R, PIN_HIGH);
        }
        else if (status->temp_alert && status->humi_alert) {
            rt_pin_write(PIN_LED_R, !rt_pin_read(PIN_LED_R));
            rt_pin_write(PIN_LED_Y, !rt_pin_read(PIN_LED_Y));
        }
    }

    /* 无报警状态 */
    if (!status->temp_alert && !status->humi_alert) {
        rt_pin_write(PIN_LED_R, PIN_HIGH);
        rt_pin_write(PIN_LED_Y, PIN_HIGH);
    }

    /* 蜂鸣器控制 */
    rt_bool_t need_beep = status->temp_alert || status->humi_alert;
    if (need_beep != status->beep_state) {
        rt_pin_write(PIN_BEEP, need_beep);
        status->beep_state = need_beep;
    }
}

/* 温湿度监控线程 */
static void env_monitor_thread(void *param)
{
    float temp, humi;
    struct alert_status status = {RT_FALSE, RT_FALSE, RT_FALSE, 0};

    hardware_init();

    while (1) {
        temp = aht10_read_temperature(dev);
        humi = aht10_read_humidity(dev);

        if (isnan(temp) || isnan(humi)) {
            rt_thread_mdelay(SAMPLE_INTERVAL);
            continue;
        }

        update_alert_status(&status, temp, humi);
        rt_thread_mdelay(SAMPLE_INTERVAL);
    }
}

/* 初始化函数 */
static int env_monitor_init(void)
{
    rt_thread_t tid;

    if (!(dev = aht10_init("i2c3"))) {
        return -RT_ERROR;
    }

    tid = rt_thread_create("env_monitor",
                         env_monitor_thread,
                         RT_NULL,
                         2048,
                         25,
                         10);
    if (tid) {
        rt_thread_startup(tid);
        return RT_EOK;
    }

    return -RT_ERROR;
}

/* 自动初始化 */
INIT_APP_EXPORT(env_monitor_init);
