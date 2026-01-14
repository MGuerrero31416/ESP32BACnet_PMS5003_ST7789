/*
 * BACnet Server Task - Corrected based on working version
 * Uses datalink_receive() and npdu_handler() API
 */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"

/* Include BACnet headers - same as your working version */
#include "bacnet_config.h"   // MUST BE FIRST for MAX_* definitions!
#include "bacdef.h"
#include "datalink.h"
#include "npdu.h"
#include "handlers.h"
#include "address.h"
#include "apdu.h"
#include "txbuf.h"

/* Include object headers for control logic */
#include "av.h"
#include "bo.h"
#include "bv.h"
#include "pm25_sensor.h"

static const char *TAG = "SERVER_TASK";

/** Buffer used for receiving */
static uint8_t rx_buffer[MAX_MPDU] = { 0 };

/** Sensor monitoring variables */
static uint32_t last_sensor_update_time = 0;
static bool sensor_has_data = false;
static const uint32_t SENSOR_TIMEOUT_MS = 30000;  // 30 seconds timeout

/** Object instance definitions (must match main.c) */
#define PM2_5_OBJECT_INSTANCE           1
#define PM2_5_SETPOINT_OBJECT_INSTANCE  3
#define FAN_COMMAND_OBJECT_INSTANCE     0
#define SENSOR_ERROR_OBJECT_INSTANCE    0

/**
 * @brief Check sensor data and control fan based on PM2.5 levels
 */
static void check_sensor_and_control_fan(void)
{
    float pm25_value = 0.0;
    float setpoint_value = 25.0;  // Default fallback
    BACNET_BINARY_PV current_fan_state;
    bool sensor_error = false;
    
    /* Get current PM2.5 value */
    if (Analog_Value_Valid_Instance(PM2_5_OBJECT_INSTANCE)) {
        pm25_value = Analog_Value_Present_Value(PM2_5_OBJECT_INSTANCE);
       // ESP_LOGI(TAG, "DEBUG: PM2.5 reading: %.1f", pm25_value);
        
        /* Update last sensor data time */
        last_sensor_update_time = (uint32_t)(esp_timer_get_time() / 1000);
        sensor_has_data = true;
    } else {
       // ESP_LOGW(TAG, "PM2.5 object instance %d not valid", PM2_5_OBJECT_INSTANCE);
    }
    
    /* Get PM2.5 setpoint */
    if (Analog_Value_Valid_Instance(PM2_5_SETPOINT_OBJECT_INSTANCE)) {
        setpoint_value = Analog_Value_Present_Value(PM2_5_SETPOINT_OBJECT_INSTANCE);
      //  ESP_LOGI(TAG, "DEBUG: PM2.5 setpoint: %.1f (instance %d)", setpoint_value, PM2_5_SETPOINT_OBJECT_INSTANCE);
    } else {
        ESP_LOGW(TAG, "PM2.5 setpoint object instance %d not valid", PM2_5_SETPOINT_OBJECT_INSTANCE);
    }
    
   // ESP_LOGI(TAG, "DEBUG: Comparison: PM2.5=%.1f, Setpoint=%.1f, Difference=%.1f", 
   //          pm25_value, setpoint_value, pm25_value - setpoint_value);
    
    /* Check sensor data freshness */
    if (sensor_has_data) {
        uint32_t current_time = (uint32_t)(esp_timer_get_time() / 1000);
        uint32_t time_since_update = current_time - last_sensor_update_time;
        
        if (time_since_update > SENSOR_TIMEOUT_MS) {
            ESP_LOGW(TAG, "Sensor data stale: %lu ms since last update", time_since_update);
            sensor_error = true;
        } else {
            sensor_error = false;
        }
    } else {
        /* No sensor data ever received */
        sensor_error = true;
        ESP_LOGW(TAG, "No sensor data received yet");
    }
    
    /* Control fan based on PM2.5 vs setpoint */
    if (Binary_Output_Valid_Instance(FAN_COMMAND_OBJECT_INSTANCE)) {
        current_fan_state = Binary_Output_Present_Value(FAN_COMMAND_OBJECT_INSTANCE);
        
        // Use a small tolerance for floating point comparison
        if (pm25_value > (setpoint_value + 0.1f)) {  // Added 0.1 tolerance
            /* PM2.5 is above setpoint - turn fan ON */
            if (current_fan_state != BINARY_ACTIVE) {
                ESP_LOGI(TAG, "ACTION: PM2.5 (%.1f) > Setpoint (%.1f) - Turning fan ON", 
                        pm25_value, setpoint_value);
                Binary_Output_Present_Value_Set(FAN_COMMAND_OBJECT_INSTANCE, BINARY_ACTIVE, 16);
            } else {
                ESP_LOGI(TAG, "Fan already ON (PM2.5=%.1f, Setpoint=%.1f)", 
                        pm25_value, setpoint_value);
            }
        } else {
            /* PM2.5 is at or below setpoint - turn fan OFF */
            if (current_fan_state != BINARY_INACTIVE) {
                ESP_LOGI(TAG, "ACTION: PM2.5 (%.1f) <= Setpoint (%.1f) - Turning fan OFF", 
                        pm25_value, setpoint_value);
                Binary_Output_Present_Value_Set(FAN_COMMAND_OBJECT_INSTANCE, BINARY_INACTIVE, 16);
            } else {
            //    ESP_LOGI(TAG, "Fan already OFF (PM2.5=%.1f, Setpoint=%.1f)", 
            //            pm25_value, setpoint_value);
            }
        }
    } else {
        ESP_LOGW(TAG, "FAN_COMMAND_OBJECT_INSTANCE %d not valid", FAN_COMMAND_OBJECT_INSTANCE);
    }
    
    /* Set sensor error state */
    if (Binary_Value_Valid_Instance(SENSOR_ERROR_OBJECT_INSTANCE)) {
        BACNET_BINARY_PV current_error_state = Binary_Value_Present_Value(SENSOR_ERROR_OBJECT_INSTANCE);
        BACNET_BINARY_PV desired_error_state = sensor_error ? BINARY_ACTIVE : BINARY_INACTIVE;
        
        if (current_error_state != desired_error_state) {
            ESP_LOGI(TAG, "Sensor error state: %s", sensor_error ? "ERROR" : "OK");
            Binary_Value_Present_Value_Set(SENSOR_ERROR_OBJECT_INSTANCE, desired_error_state);
        }
    } else {
        ESP_LOGW(TAG, "SENSOR_ERROR_OBJECT_INSTANCE %d not valid", SENSOR_ERROR_OBJECT_INSTANCE);
    }
}

/**
 * @brief Initialize sensor monitoring
 * 
 * Call this once at startup to initialize monitoring variables
 */
static void init_sensor_monitoring(void)
{
    last_sensor_update_time = (uint32_t)(esp_timer_get_time() / 1000);
    sensor_has_data = false;
    ESP_LOGI(TAG, "Sensor monitoring initialized");
}

void server_task(void *arg)
{
    BACNET_ADDRESS src = { 0 }; 
    uint16_t pdu_len = 0;
    uint32_t last_check_time = 0;
    const uint32_t CHECK_INTERVAL_MS = 5000;  // Check every 5 seconds
    
    ESP_LOGI(TAG, "BACnet server task started");
    
    /* Initialize sensor monitoring */
    init_sensor_monitoring();
    
    /* Get initial time */
    last_check_time = (uint32_t)(esp_timer_get_time() / 1000);

    for (;;) {
        uint32_t current_time = (uint32_t)(esp_timer_get_time() / 1000);
        
        /* Receive BACnet packet with 100ms timeout (reduced from 5 seconds) */
        pdu_len = datalink_receive(&src, &rx_buffer[0], MAX_MPDU, 100);

        if (pdu_len) {
            /* Process the received packet */
            npdu_handler(&src, &rx_buffer[0], pdu_len);
        }
        
        /* Check sensor and control fan periodically */
        if ((current_time - last_check_time) >= CHECK_INTERVAL_MS) {
            check_sensor_and_control_fan();
            last_check_time = current_time;
            
            /* Log current states for debugging */
            if (sensor_has_data) {
                float pm25 = 0.0;
                float setpoint = 0.0;
                
                if (Analog_Value_Valid_Instance(PM2_5_OBJECT_INSTANCE)) {
                    pm25 = Analog_Value_Present_Value(PM2_5_OBJECT_INSTANCE);
                }
                
                if (Analog_Value_Valid_Instance(PM2_5_SETPOINT_OBJECT_INSTANCE)) {
                    setpoint = Analog_Value_Present_Value(PM2_5_SETPOINT_OBJECT_INSTANCE);
                }
                
                ESP_LOGD(TAG, "Monitoring: PM2.5=%.1f, Setpoint=%.1f", pm25, setpoint);
            }
        }
        
        /* Small delay to prevent watchdog */
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}