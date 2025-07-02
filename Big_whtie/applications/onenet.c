#include <rtthread.h>
#include <rtdevice.h>
#include <board.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <onenet.h>

#include "aht10.h"
#include "ap3216c.h"

#define DBG_ENABLE
#define DBG_COLOR
#define DBG_SECTION_NAME    "onenet.sample"
#if ONENET_DEBUG
#define DBG_LEVEL           DBG_LOG
#else
#define DBG_LEVEL           DBG_INFO
#endif /* ONENET_DEBUG */

#include <rtdbg.h>

#define PIN_LED_B              GET_PIN(F, 11)      // PF11 :  LED_B        --> LED
#define PIN_LED_R              GET_PIN(F, 12)      // PF12 :  LED_R        --> LED

#ifdef FINSH_USING_MSH
#include <finsh.h>

/* upload random value to temperature*/
static void onenet_upload_entry(void *parameter)
{
    float humidity, temperature, brightness;
    char temp_str[16];
    char humi_str[16];
    char bright_str[16];
    aht10_device_t dev1;
    ap3216c_device_t dev2;
    /* 总线名称 */
    const char *i2c_bus_name1 = "i2c2";
    /* 初始化 ap3216c */
    dev2 = ap3216c_init(i2c_bus_name1);
    if (dev2 == RT_NULL)
    {
        LOG_E("ap3216c initializes failure.");
    }
    const char *i2c_bus_name2 = "i2c3";
    /* 等待传感器正常工作 */
    rt_thread_mdelay(2000);
    /* 初始化 aht10 */
    dev1 = aht10_init(i2c_bus_name2);
    if (dev1 == RT_NULL)
    {
        LOG_E("aht10 initializes failure");
    }
    while (1)
    {
        temperature = aht10_read_temperature(dev1);
        humidity = aht10_read_humidity(dev1);
        brightness = ap3216c_read_ambient_light(dev2);
        // 四舍五入到两位小数
        temperature = (float)(((int)(temperature * 100 + 0.5)) / 100.0);
        humidity = (float)(((int)(humidity * 100 + 0.5)) / 100.0);
        brightness = (float)(((int)(brightness * 100 + 0.5)) / 100.0);
        // 手动分离整数和小数部分
        int temp_int = (int)temperature;
        int temp_dec = (int)((temperature - temp_int) * 100);
        int humi_int = (int)humidity;
        int humi_dec = (int)((humidity - humi_int) * 100);
        int bright_int = (int)brightness;
        int bright_dec = (int)((brightness - bright_int) * 100);
        // 构造字符串
        rt_snprintf(temp_str, sizeof(temp_str), "%d.%02d", temp_int, temp_dec);
        rt_snprintf(humi_str, sizeof(humi_str), "%d.%02d", humi_int, humi_dec);
        rt_snprintf(bright_str, sizeof(bright_str), "%d.%02d", bright_int, bright_dec);
        // 使用字符串上传
        if (onenet_mqtt_upload_string("temperature", temp_str) < 0)
        {
            LOG_E("temperature upload has an error, stop uploading");
            break;
        }
        else
        {
            LOG_D("buffer : {\"temperature\":\"%s\"}", temp_str);
        }
        rt_thread_mdelay(500);
        if (onenet_mqtt_upload_string("humidity", humi_str) < 0)
        {
            LOG_E("humidity upload has an error, stop uploading");
            break;
        }
        else
        {
            LOG_D("buffer : {\"humidity\":\"%s\"}", humi_str);
        }
        rt_thread_mdelay(500);
        if (onenet_mqtt_upload_string("brightness", bright_str) < 0)
        {
            LOG_E("brightness upload has an error, stop uploading");
            // 处理上传失败逻辑
        }
        else
        {
            LOG_D("buffer : {\"brightness\":\"%s\"}", bright_str);
        }
        rt_thread_delay(rt_tick_from_millisecond(5 * 1000));
    }
}

int onenet_upload_cycle(void)
{
    rt_thread_t tid;

    tid = rt_thread_create("onenet_send",
                           onenet_upload_entry,
                           RT_NULL,
                           2 * 1024,
                           RT_THREAD_PRIORITY_MAX / 3 - 1,
                           5);
    if (tid)
    {
        rt_thread_startup(tid);
    }

    return 0;
}
MSH_CMD_EXPORT(onenet_upload_cycle, send data to OneNET cloud cycle);

int onenet_publish_digit(int argc, char **argv)
{
    if (argc != 3)
    {
        LOG_E("onenet_publish [datastream_id]  [value]  - mqtt pulish digit data to OneNET.");
        return -1;
    }

    if (onenet_mqtt_upload_digit(argv[1], atoi(argv[2])) < 0)
    {
        LOG_E("upload digit data has an error!\n");
    }

    return 0;
}
MSH_CMD_EXPORT_ALIAS(onenet_publish_digit, onenet_mqtt_publish_digit, send digit data to onenet cloud);

int onenet_publish_string(int argc, char **argv)
{
    if (argc != 3)
    {
        LOG_E("onenet_publish [datastream_id]  [string]  - mqtt pulish string data to OneNET.");
        return -1;
    }

    if (onenet_mqtt_upload_string(argv[1], argv[2]) < 0)
    {
        LOG_E("upload string has an error!\n");
    }

    return 0;
}
MSH_CMD_EXPORT_ALIAS(onenet_publish_string, onenet_mqtt_publish_string, send string data to onenet cloud);

/* onenet mqtt command response callback function */
static void onenet_cmd_rsp_cb(uint8_t *recv_data, size_t recv_size, uint8_t **resp_data, size_t *resp_size)
{
    char res_buf[] = { "cmd is received!\n" };

    LOG_D("recv data is %x:%x\n", recv_data[0], recv_data[1]);
    
    if(recv_data[0] == 0x00)
    {
        rt_pin_write(PIN_LED_B, recv_data[1] > 0 ? PIN_HIGH : PIN_LOW);
        LOG_D("blue light %d", recv_data[1]);
    }else if(recv_data[0] == 0x01)
    {
        rt_pin_write(PIN_LED_R, recv_data[1] > 0 ? PIN_HIGH : PIN_LOW);
        LOG_D("red light %d", recv_data[1]);
    }

    /* user have to malloc memory for response data */
    *resp_data = (uint8_t *) ONENET_MALLOC(strlen(res_buf));

    strncpy((char *)*resp_data, res_buf, strlen(res_buf));

    *resp_size = strlen(res_buf);
}

/* set the onenet mqtt command response callback function */
int onenet_set_cmd_rsp(int argc, char **argv)
{
    rt_pin_mode(PIN_LED_B,PIN_MODE_OUTPUT);
    rt_pin_mode(PIN_LED_R,PIN_MODE_OUTPUT);
    onenet_set_cmd_rsp_cb(onenet_cmd_rsp_cb);
    return 0;
}
MSH_CMD_EXPORT(onenet_set_cmd_rsp, set cmd response function);

#endif /* FINSH_USING_MSH */
