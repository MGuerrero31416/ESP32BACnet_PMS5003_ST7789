#include "pm25_sensor.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "driver/uart.h"

static const char *TAG = "pm25_sensor";

// PMS5003 data structure
typedef struct {
    uint16_t framelen;
    uint16_t pm10_standard, pm25_standard, pm100_standard;
    uint16_t pm10_env, pm25_env, pm100_env;
    uint16_t particles_03um, particles_05um, particles_10um, particles_25um, particles_50um, particles_100um;
    uint16_t unused;
    uint16_t checksum;
} pms5003_data_t;

// Global variables to store all PM values
static float current_pm1_0 = 0.0f;
static float current_pm2_5 = 0.0f;
static float current_pm10 = 0.0f;
static SemaphoreHandle_t pm_mutex = NULL;

// UART configuration for PMS5003
#define PMS_UART_NUM           UART_NUM_1
#define PMS_RX_PIN             25
#define PMS_TX_PIN             26
#define PMS_UART_BAUD_RATE     9600
#define PMS_UART_BUFFER_SIZE   1024

// Initialize UART for PMS5003
static esp_err_t pms5003_uart_init(void)
{
    uart_config_t uart_config = {
        .baud_rate = PMS_UART_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_APB,
    };
    
    esp_err_t ret = uart_driver_install(PMS_UART_NUM, PMS_UART_BUFFER_SIZE * 2, 0, 0, NULL, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to install UART driver");
        return ret;
    }
    
    ret = uart_param_config(PMS_UART_NUM, &uart_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure UART parameters");
        return ret;
    }
    
    ret = uart_set_pin(PMS_UART_NUM, PMS_TX_PIN, PMS_RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set UART pins");
        return ret;
    }
    
    return ESP_OK;
}

// Read data from PMS5003
static esp_err_t pms5003_read_data(pms5003_data_t *data)
{
    uint8_t buffer[32];
    int length = 0;
    
    // Read until we get start bytes 0x42 0x4D
    while (1) {
        length = uart_read_bytes(PMS_UART_NUM, buffer, 2, pdMS_TO_TICKS(1000));
        if (length == 2 && buffer[0] == 0x42 && buffer[1] == 0x4D) {
            break;
        }
    }
    
    // Read the rest of the frame (30 bytes)
    length = uart_read_bytes(PMS_UART_NUM, buffer, 30, pdMS_TO_TICKS(1000));
    if (length != 30) {
        ESP_LOGE(TAG, "Failed to read complete frame");
        return ESP_FAIL;
    }
    
    // Parse data
    data->framelen = (buffer[0] << 8) | buffer[1];
    data->pm10_standard = (buffer[2] << 8) | buffer[3];
    data->pm25_standard = (buffer[4] << 8) | buffer[5];
    data->pm100_standard = (buffer[6] << 8) | buffer[7];
    data->pm10_env = (buffer[8] << 8) | buffer[9];
    data->pm25_env = (buffer[10] << 8) | buffer[11];
    data->pm100_env = (buffer[12] << 8) | buffer[13];
    data->particles_03um = (buffer[14] << 8) | buffer[15];
    data->particles_05um = (buffer[16] << 8) | buffer[17];
    data->particles_10um = (buffer[18] << 8) | buffer[19];
    data->particles_25um = (buffer[20] << 8) | buffer[21];
    data->particles_50um = (buffer[22] << 8) | buffer[23];
    data->particles_100um = (buffer[24] << 8) | buffer[25];
    data->unused = (buffer[26] << 8) | buffer[27];
    data->checksum = (buffer[28] << 8) | buffer[29];
    
    // Verify checksum
    uint16_t sum = 0x42 + 0x4D;
    for (int i = 0; i < 28; i++) {
        sum += buffer[i];
    }
    
    if (sum != data->checksum) {
        ESP_LOGE(TAG, "Checksum error: calculated 0x%04X, received 0x%04X", sum, data->checksum);
        return ESP_FAIL;
    }
    
    return ESP_OK;
}

// Task to read data from PMS5003 sensor
static void pms5003_read_task(void *pvParameters)
{
    pms5003_data_t data;
    
    ESP_LOGI(TAG, "PMS5003 sensor task started");
    
    for (;;) {
        if (pms5003_read_data(&data) == ESP_OK) {
            // Store all PM values with mutex protection
            if (pm_mutex != NULL) {
                if (xSemaphoreTake(pm_mutex, portMAX_DELAY) == pdTRUE) {
                    
                    current_pm1_0 = (float)data.pm10_standard;    // Note: pm10_standard is actually PM1.0
                    current_pm2_5 = (float)data.pm25_standard;
                    current_pm10 = (float)data.pm100_standard;
                    xSemaphoreGive(pm_mutex);
                    
        //            ESP_LOGI(TAG, "PM1.0: %.1f, PM2.5: %.1f, PM10: %.1f μg/m³", 
        //                    (float)data.pm10_standard, 
        //                   (float)data.pm25_standard,
        //                    (float)data.pm100_standard);
                }
            } else {
                current_pm1_0 = (float)data.pm10_standard;
                current_pm2_5 = (float)data.pm25_standard;
                current_pm10 = (float)data.pm100_standard;
            }
        } else {
            ESP_LOGW(TAG, "Failed to read from PMS5003");
        }
        
        // Read every 5 seconds
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

// Public function to initialize the PM sensor
void pm25_sensor_init(void)
{
    // Create mutex for PM data protection
    pm_mutex = xSemaphoreCreateMutex();
    if (pm_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create PM mutex");
    }
    
    // Initialize UART
    if (pms5003_uart_init() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize UART for PMS5003");
        return;
    }
    
    // Start PMS5003 reading task
    xTaskCreate(pms5003_read_task, "pms5003_read", 4096, NULL, 5, NULL);
    
    ESP_LOGI(TAG, "PM sensor initialized");
}

// Public function to get PM1.0 value
float pm25_get_pm1_0(void)
{
    float value = 0.0f;
    
    if (pm_mutex != NULL) {
        if (xSemaphoreTake(pm_mutex, portMAX_DELAY) == pdTRUE) {
            value = current_pm1_0;
            xSemaphoreGive(pm_mutex);
        }
    } else {
        value = current_pm1_0;
    }
    
    return value;

}

// Public function to get PM2.5 value
float pm25_get_pm2_5(void)
{
    float value = 0.0f;
    
    if (pm_mutex != NULL) {
        if (xSemaphoreTake(pm_mutex, portMAX_DELAY) == pdTRUE) {
            value = current_pm2_5;
            xSemaphoreGive(pm_mutex);
        }
    } else {
        value = current_pm2_5;
    }
    
    return value;

}

// Public function to get PM10 value
float pm25_get_pm10(void)
{
    float value = 0.0f;
    
    if (pm_mutex != NULL) {
        if (xSemaphoreTake(pm_mutex, portMAX_DELAY) == pdTRUE) {
            value = current_pm10;
            xSemaphoreGive(pm_mutex);
        }
    } else {
        value = current_pm10;
    }
    
    return value;

}