/*
 * Programm um die Temperatur des internen Sensors "SHTC3" und eines 
 * externen Sensors "MLX90614"auszulesen und auf dem direkt am 
 * ESP32-C Sensort gesteckten Displays auszugeben.
 * Zusätzlich werden über ESPNow alle 30Sec die gemessenen 
 * Daten an einen weiteren ESP32 Chip gesendet.
 */

#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "driver/i2c_master.h"
#include "driver/i2c.h"

 
 
 
 // I2C Interner Sensor + Display
#define I2C_NUM_0 0
#define SHTC3_SENSOR_ADDR 0x70

//static const char *TAG = "TEMP_SHTC3_SENSOR";


int shtc3_read_temp(float *temp_out);

 
 void initI2C(i2c_port_t i2c_num)
{
    //ESP_LOGI(TAG, "init I2C");
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = 5,
        .scl_io_num = 6,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = 50000};
    i2c_param_config(i2c_num, &conf);
    ESP_ERROR_CHECK(i2c_driver_install(i2c_num, conf.mode, 0, 0, 0));
}
 
 // i2c_scanner um Mitglieder zu finden (Suche für externer Sensor)
 int i2c_scan(void)
{
    printf("Scanning I2C bus...\n");
    for (uint8_t addr = 1; addr < 127; ++addr)
    {
        i2c_cmd_handle_t cmd = i2c_cmd_link_create();
        i2c_master_start(cmd);
        i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_WRITE, true);
        i2c_master_stop(cmd);
        esp_err_t err = i2c_master_cmd_begin(I2C_NUM_0, cmd, pdMS_TO_TICKS(100));
        i2c_cmd_link_delete(cmd);
        if (err == ESP_OK)
        {
            printf("Found I2C device at 0x%02X\n", addr);
        }
    }
    printf("Scan complete.\n");
    return 0;
}
 
 // Interner Sensor 
 int shtc3_read_temp(float *temp_out)
 {
    uint8_t data[6];
    uint8_t command[2] = {0x78, 0x66}; // Wake-up command 
    esp_err_t ret;
    ret = i2c_master_write_to_device(I2C_NUM_0, SHTC3_SENSOR_ADDR, command, 2, -1);
    vTaskDelay(pdMS_TO_TICKS(20));
    if (ret != ESP_OK)
    {
        //ESP_LOGE(TAG, "I2C Write failed: %s", esp_err_to_name(ret));
        temp_out[0] = temp_out[1] = -999.0f;
        return 1;
    }
    ret = i2c_master_read_from_device(I2C_NUM_0, SHTC3_SENSOR_ADDR, data, 6, -1);
    if (ret != ESP_OK)
    {
        //ESP_LOGE(TAG, "I2C Read failed: %s", esp_err_to_name(ret));
        temp_out[0] = temp_out[1] = -999.0f;
        return 2;
    }
    uint16_t rt = 0;
    uint16_t rh = 0;
    rt = data[0] << 8 | data[1];
    rh = data[3] << 8 | data[4];
    // printf("raw_temp = %d, raw_humi = %d\r\n", rt, rh);

    temp_out[0] = (float)(-45 + 175 * rt / 65536); // Temperatur
    temp_out[1] = (float)(rh * 100 / 65536);	   // Relative Feuchte [%]
    return 0;
 }
 
 