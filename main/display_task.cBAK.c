/*
 * Display Task for BACnet PM2.5 Monitor - NO FLICKER with smart updates
 */
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "display_driver.h"


/* BACnet includes */
#include "bacdef.h"
#include "av.h"
#include "bo.h"
#include "bv.h"
#include "bi.h"  // ADD THIS for Binary_Input support

static const char *TAG = "DISPLAY_TASK";

/* Display layout */
#define LINE_HEIGHT      10
#define LINE_SPACING     2
#define LEFT_MARGIN      5
#define TOP_MARGIN       5

/* Consistent Y positions for all lines */
#define LINE1_Y (TOP_MARGIN)                                // ID: 123456
#define LINE2_Y (LINE1_Y + LINE_HEIGHT + LINE_SPACING)      // IP:
#define LINE3_Y (LINE2_Y + LINE_HEIGHT + LINE_SPACING + LINE_HEIGHT)  // PM1.0: (after space)
#define LINE4_Y (LINE3_Y + LINE_HEIGHT + LINE_SPACING)      // PM2.5:
#define LINE5_Y (LINE4_Y + LINE_HEIGHT + LINE_SPACING)      // PM10:
#define LINE6_Y (LINE5_Y + LINE_HEIGHT + LINE_SPACING + LINE_HEIGHT)  // Setpoint: (after space)
#define LINE7_Y (LINE6_Y + LINE_HEIGHT + LINE_SPACING + LINE_HEIGHT)  // FAN ON/OFF: (after space)
#define LINE8_Y (LINE7_Y + LINE_HEIGHT + LINE_SPACING)      // FAN STATUS:
#define LINE9_Y (LINE8_Y + LINE_HEIGHT + LINE_SPACING)      // Sensor Error:

/* X positions for dynamic data (right of labels) */
#define DATA_X_PM        65    // X position for PM values
#define DATA_X_SETPOINT  75    // X position for setpoint
#define DATA_X_IP        35    // X position for IP address
#define DATA_X_FAN       85    // X position for FAN status
#define DATA_X_ERROR     95    // X position for sensor error

/* Object instance definitions */
#define PM1_0_OBJECT_INSTANCE           0
#define PM2_5_OBJECT_INSTANCE           1
#define PM10_OBJECT_INSTANCE            2
#define PM2_5_SETPOINT_INSTANCE         3
#define FAN_COMMAND_OBJECT_INSTANCE     0    // Binary_Output: Command to control fan
#define FAN_STATUS_OBJECT_INSTANCE      0    // Binary_Input: Actual fan status from GPIO
#define SENSOR_ERROR_OBJECT_INSTANCE    0

// Cache for tracking changes
typedef struct {
    char ip_addr[20];
    float pm10;
    float pm25;
    float pm100;
    float setpoint;
    char fan_cmd[10];        // FAN ON/OFF (command)
    char fan_status[10];     // FAN STATUS (actual GPIO state)
    char sensor_error[10];
} display_cache_t;

static display_cache_t last_display = {0};
static bool screen_initialized = false;

// ========== HELPER FUNCTIONS ==========

/**
 * @brief Get device IP address as string
 */
static void get_device_ip(char *ip_str, size_t max_len)
{
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (netif) {
        esp_netif_ip_info_t ip_info;
        if (esp_netif_get_ip_info(netif, &ip_info) == ESP_OK) {
            snprintf(ip_str, max_len, IPSTR, IP2STR(&ip_info.ip));
        } else {
            strncpy(ip_str, "No IP", max_len);
        }
    } else {
        strncpy(ip_str, "No WiFi", max_len);
    }
}

/**
 * @brief Get PM1.0 value from BACnet object
 */
static float get_pm10_value(void)
{
    if (Analog_Value_Valid_Instance(PM1_0_OBJECT_INSTANCE)) {
        return Analog_Value_Present_Value(PM1_0_OBJECT_INSTANCE);
    }
    return 0.0;
}

/**
 * @brief Get PM2.5 value from BACnet object
 */
static float get_pm25_value(void)
{
    if (Analog_Value_Valid_Instance(PM2_5_OBJECT_INSTANCE)) {
        return Analog_Value_Present_Value(PM2_5_OBJECT_INSTANCE);
    }
    return 0.0;
}

/**
 * @brief Get PM10 value from BACnet object
 */
static float get_pm100_value(void)
{
    if (Analog_Value_Valid_Instance(PM10_OBJECT_INSTANCE)) {
        return Analog_Value_Present_Value(PM10_OBJECT_INSTANCE);
    }
    return 0.0;
}

/**
 * @brief Get PM2.5 setpoint value from BACnet object
 */
static float get_pm25_setpoint(void)
{
    if (Analog_Value_Valid_Instance(PM2_5_SETPOINT_INSTANCE)) {
        return Analog_Value_Present_Value(PM2_5_SETPOINT_INSTANCE);
    }
    return 0.0;
}

/**
 * @brief Get fan command status (ON/OFF)
 */
static const char* get_fan_command_status(void)
{
    if (Binary_Output_Valid_Instance(FAN_COMMAND_OBJECT_INSTANCE)) {
        BACNET_BINARY_PV state = Binary_Output_Present_Value(FAN_COMMAND_OBJECT_INSTANCE);
        return (state == BINARY_ACTIVE) ? "ON" : "OFF";
    }
    return "ERR";
}

/**
 * @brief Get actual fan status from GPIO input (Binary_Input)
 */
static const char* get_fan_actual_status(void)
{
    if (Binary_Input_Valid_Instance(FAN_STATUS_OBJECT_INSTANCE)) {
        BACNET_BINARY_PV state = Binary_Input_Present_Value(FAN_STATUS_OBJECT_INSTANCE);
        return (state == BINARY_ACTIVE) ? "ON" : "OFF";
    }
    return "ERR";
}

/**
 * @brief Get sensor error status
 */
static const char* get_sensor_error_status(void)
{
    if (Binary_Value_Valid_Instance(SENSOR_ERROR_OBJECT_INSTANCE)) {
        BACNET_BINARY_PV state = Binary_Value_Present_Value(SENSOR_ERROR_OBJECT_INSTANCE);
        return (state == BINARY_ACTIVE) ? "ERROR" : "OK";
    }
    return "ERR";
}

// ========== DISPLAY FUNCTIONS ==========

/**
 * @brief Draw only what changed (SMART UPDATE - NO FLICKER)
 */
static void smart_update_display(void)
{
    char buffer[50];
    display_cache_t current;
    
    // Get all current values
    get_device_ip(current.ip_addr, sizeof(current.ip_addr));
    current.pm10 = get_pm10_value();
    current.pm25 = get_pm25_value();
    current.pm100 = get_pm100_value();
    current.setpoint = get_pm25_setpoint();
    
    const char *fan_cmd = get_fan_command_status();
    const char *fan_actual = get_fan_actual_status();
    const char *sensor_status = get_sensor_error_status();
    
    strncpy(current.fan_cmd, fan_cmd, sizeof(current.fan_cmd));
    strncpy(current.fan_status, fan_actual, sizeof(current.fan_status));
    strncpy(current.sensor_error, sensor_status, sizeof(current.sensor_error));
    
    // If first time, draw everything
    if (!screen_initialized) {
        display_clear(DISP_BLACK);
        
        // Draw static labels once using consistent positions
        display_draw_string(LEFT_MARGIN, LINE1_Y, "ID: 123456", DISP_WHITE, DISP_BLACK);
        display_draw_string(LEFT_MARGIN, LINE2_Y, "IP:", DISP_WHITE, DISP_BLACK);
        display_draw_string(LEFT_MARGIN, LINE3_Y, "PM1.0:", DISP_CYAN, DISP_BLACK);
        display_draw_string(LEFT_MARGIN, LINE4_Y, "PM2.5:", DISP_WHITE, DISP_BLACK);
        display_draw_string(LEFT_MARGIN, LINE5_Y, "PM10:", DISP_CYAN, DISP_BLACK);
        display_draw_string(LEFT_MARGIN, LINE6_Y, "Setpoint:", DISP_WHITE, DISP_BLACK);
        display_draw_string(LEFT_MARGIN, LINE7_Y, "FAN ON/OFF:", DISP_WHITE, DISP_BLACK);
        display_draw_string(LEFT_MARGIN, LINE8_Y, "FAN STATUS:", DISP_WHITE, DISP_BLACK);
        display_draw_string(LEFT_MARGIN, LINE9_Y, "Sensor Error:", DISP_WHITE, DISP_BLACK);
        
        screen_initialized = true;
    }
    
    // Update ONLY changing values
    
    // 1. Update IP (if changed)
    if (strcmp(last_display.ip_addr, current.ip_addr) != 0) {
        // Clear old IP area - WIDER to accommodate IP addresses
        display_fill_rect(DATA_X_IP, LINE2_Y, 100, LINE_HEIGHT, DISP_BLACK);
        // Draw new IP
        display_draw_string(DATA_X_IP, LINE2_Y, current.ip_addr, DISP_WHITE, DISP_BLACK);
    }
    
    // 2. Update PM1.0 (if changed by > 0.1)
    if (fabsf(last_display.pm10 - current.pm10) > 0.1f) {
        snprintf(buffer, sizeof(buffer), "%.1f ug/m3", current.pm10);
        // Clear old value area - WIDER for the full value
        display_fill_rect(DATA_X_PM, LINE3_Y, 80, LINE_HEIGHT, DISP_BLACK);
        // Draw new value
        display_draw_string(DATA_X_PM, LINE3_Y, buffer, DISP_CYAN, DISP_BLACK);
    }
    
    // 3. Update PM2.5 (if changed by > 0.1)
    if (fabsf(last_display.pm25 - current.pm25) > 0.1f) {
        snprintf(buffer, sizeof(buffer), "%.1f ug/m3", current.pm25);
        // Clear old value area
        display_fill_rect(DATA_X_PM, LINE4_Y, 80, LINE_HEIGHT, DISP_BLACK);
        // Determine color
        uint16_t pm25_color = DISP_GREEN;
        if (current.pm25 > 35.0f) pm25_color = DISP_RED;
        else if (current.pm25 > 12.0f) pm25_color = DISP_YELLOW;
        // Draw new value
        display_draw_string(DATA_X_PM, LINE4_Y, buffer, pm25_color, DISP_BLACK);
    }
    
    // 4. Update PM10 (if changed by > 0.1)
    if (fabsf(last_display.pm100 - current.pm100) > 0.1f) {
        snprintf(buffer, sizeof(buffer), "%.1f ug/m3", current.pm100);
        // Clear old value area
        display_fill_rect(DATA_X_PM, LINE5_Y, 80, LINE_HEIGHT, DISP_BLACK);
        // Draw new value
        display_draw_string(DATA_X_PM, LINE5_Y, buffer, DISP_CYAN, DISP_BLACK);
    }
    
    // 5. Update Setpoint (if changed by > 0.1)
    if (fabsf(last_display.setpoint - current.setpoint) > 0.1f) {
        snprintf(buffer, sizeof(buffer), "%.1f ug/m3", current.setpoint);
        // Clear old value area
        display_fill_rect(DATA_X_SETPOINT, LINE6_Y, 80, LINE_HEIGHT, DISP_BLACK);
        // Draw new value
        display_draw_string(DATA_X_SETPOINT, LINE6_Y, buffer, DISP_WHITE, DISP_BLACK);
    }
    
    // 6. Update Fan command (if changed)
    if (strcmp(last_display.fan_cmd, current.fan_cmd) != 0) {
        uint16_t fan_color = (strcmp(current.fan_cmd, "ON") == 0) ? DISP_GREEN : DISP_WHITE;
        
        // Clear and update FAN ON/OFF (command)
        display_fill_rect(DATA_X_FAN, LINE7_Y, 30, LINE_HEIGHT, DISP_BLACK);
        display_draw_string(DATA_X_FAN, LINE7_Y, current.fan_cmd, fan_color, DISP_BLACK);
    }
    
    // 7. Update Fan actual status (if changed)
    if (strcmp(last_display.fan_status, current.fan_status) != 0) {
        uint16_t status_color = (strcmp(current.fan_status, "ON") == 0) ? DISP_GREEN : DISP_WHITE;
        
        // Clear and update FAN STATUS (actual GPIO state)
        display_fill_rect(DATA_X_FAN, LINE8_Y, 30, LINE_HEIGHT, DISP_BLACK);
        display_draw_string(DATA_X_FAN, LINE8_Y, current.fan_status, status_color, DISP_BLACK);
    }
    
    // 8. Update Sensor Error (if changed)
    if (strcmp(last_display.sensor_error, current.sensor_error) != 0) {
        uint16_t error_color = (strcmp(current.sensor_error, "ERROR") == 0) ? DISP_RED : DISP_GREEN;
        
        // Clear old value area
        display_fill_rect(DATA_X_ERROR, LINE9_Y, 40, LINE_HEIGHT, DISP_BLACK);
        // Draw new value
        display_draw_string(DATA_X_ERROR, LINE9_Y, current.sensor_error, error_color, DISP_BLACK);
    }
    
    // Save current values
    memcpy(&last_display, &current, sizeof(last_display));
    
   // ESP_LOGI(TAG, "Smart update: PM2.5=%.1f, Setpoint=%.1f, FanCmd=%s, FanStatus=%s", 
   //          current.pm25, current.setpoint, current.fan_cmd, current.fan_status);
}

/**
 * @brief Display task main function - SMART UPDATES, NO FLICKER
 */
void display_task(void *arg)
{
    ESP_LOGI(TAG, "Display task starting (smart updates, no flicker)");
    
    // Initialize display
    if (display_init() != 0) {
        ESP_LOGE(TAG, "Failed to initialize display");
        vTaskDelete(NULL);
        return;
    }
    
    // Set backlight
    display_set_backlight(80);
    
    // Show startup message
    display_clear(DISP_BLACK);
    display_draw_string(LEFT_MARGIN, 50, "BACnet Monitor", DISP_WHITE, DISP_BLACK);
    display_draw_string(LEFT_MARGIN, 70, "Starting...", DISP_GREEN, DISP_BLACK);
    
    vTaskDelay(pdMS_TO_TICKS(2000));
    
    // Initialize cache with impossible values to force first draw
    last_display.pm10 = -100.0f;
    last_display.pm25 = -100.0f;
    last_display.pm100 = -100.0f;
    last_display.setpoint = -100.0f;
    strcpy(last_display.fan_cmd, "INIT");
    strcpy(last_display.fan_status, "INIT");
    strcpy(last_display.sensor_error, "INIT");
    
    TickType_t last_update_time = xTaskGetTickCount();
    const TickType_t UPDATE_INTERVAL_TICKS = pdMS_TO_TICKS(3000);
    
    while (1) {
        TickType_t current_time = xTaskGetTickCount();
        
        if ((current_time - last_update_time) >= UPDATE_INTERVAL_TICKS) {
            smart_update_display();  // Update only what changed
            last_update_time = current_time;
        }
        
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}