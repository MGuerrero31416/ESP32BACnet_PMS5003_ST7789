// main.c - Updated for all BACnet objects
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_system.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "bacnet_config.h"
#include "server_task.h"
#include "wifi.h"
#include "device.h"
#include "config.h"
#include "address.h"
#include "bacdef.h"
#include "handlers.h"
#include "client.h"
#include "dlenv.h"
#include "bacdcode.h"
#include "npdu.h"
#include "apdu.h"
#include "iam.h"
#include "tsm.h"
#include "datalink.h"
#include "dcc.h"
#include "getevent.h"
#include "net.h"
#include "txbuf.h"
#include "version.h"
#include "av.h"
#include "bi.h"
#include "bo.h"
#include "bv.h"
#include "sdkconfig.h"
#include "pm25_sensor.h"            // Our PM sensor module
#include "config.h"
#include "display_task.h"

#define SERVER_DEVICE_ID 555666  // Your BACnet device ID

// Analog Value instances
#define PM1_0_OBJECT_INSTANCE          0      // Instance 0 for PM1.0
#define PM2_5_OBJECT_INSTANCE          1      // Instance 1 for PM2.5
#define PM10_OBJECT_INSTANCE           2      // Instance 2 for PM10
#define PM2_5_SETPOINT_OBJECT_INSTANCE 3      // Instance 3 for PM2.5_SETPOINT

// Binary Input instances
#define FAN_STATUS_OBJECT_INSTANCE     0      // Instance 0 for FAN_STATUS

// Binary Output instances  
#define FAN_COMMAND_OBJECT_INSTANCE    0      // Instance 0 for FAN_COMMAND

// Binary Value instances
#define SENSOR_ERROR_OBJECT_INSTANCE   0      // Instance 0 for SENSOR_ERROR

static const char *TAG = "main";

static void Init_Service_Handlers(void);


void app_main(void)
{
    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Initialize networking stack
    ESP_ERROR_CHECK(esp_netif_init());

    // Initialize event loop
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // Initialize wifi and connect
    wifi_initialize();

    // Initialize PM sensor
    pm25_sensor_init();

    // Initialize BACnet objects FIRST
    ESP_LOGI(TAG, "Initializing BACnet objects...");
    Analog_Value_Init();
    Binary_Input_Init();
    Binary_Output_Init();
    Binary_Value_Init();

    // Verify the setpoint is initialized to 25.0
    float setpoint = Analog_Value_Present_Value(PM2_5_SETPOINT_OBJECT_INSTANCE);
    ESP_LOGI(TAG, "Initial PM2.5 setpoint value: %.1f", setpoint);

    // You can also explicitly set it to ensure it's 25.0
    Analog_Value_Present_Value_Set(PM2_5_SETPOINT_OBJECT_INSTANCE, 25.0, 16);
    setpoint = Analog_Value_Present_Value(PM2_5_SETPOINT_OBJECT_INSTANCE);
    ESP_LOGI(TAG, "After explicit set, PM2.5 setpoint: %.1f", setpoint);

    /* allow the device ID to be set */
    Device_Set_Object_Instance_Number(SERVER_DEVICE_ID);

    printf("BACnet Server Demo with PMS5003 Air Quality Sensor\n" 
           "BACnet Stack Version %s\n"
           "BACnet Device ID: %lu\n" 
           "Max APDU: %d\n\n",
           BACnet_Version,
           Device_Object_Instance_Number(), 
           MAX_APDU);

    printf("Analog Value Objects:\n");
    printf("  Instance %d: PM1.0 Concentration\n", PM1_0_OBJECT_INSTANCE);
    printf("  Instance %d: PM2.5 Concentration\n", PM2_5_OBJECT_INSTANCE);
    printf("  Instance %d: PM10 Concentration\n", PM10_OBJECT_INSTANCE);
    printf("  Instance %d: PM2.5_SETPOINT (Default: 25.0 μg/m³)\n\n", PM2_5_SETPOINT_OBJECT_INSTANCE);

    printf("Binary Input Objects:\n");
    printf("  Instance %d: FAN_STATUS\n\n", FAN_STATUS_OBJECT_INSTANCE);

    printf("Binary Output Objects:\n");
    printf("  Instance %d: FAN_COMMAND\n\n", FAN_COMMAND_OBJECT_INSTANCE);

    printf("Binary Value Objects:\n");
    printf("  Instance %d: SENSOR_ERROR\n", SENSOR_ERROR_OBJECT_INSTANCE);

    /* load any static address bindings to show up
      in our device bindings list */
    address_init();

    Init_Service_Handlers();

    printf("[DEBUG] Total Analog Value objects registered: %u\n", Analog_Value_Count());
    printf("[DEBUG] Total Binary Input objects registered: %u\n", Binary_Input_Count());
    printf("[DEBUG] Total Binary Output objects registered: %u\n", Binary_Output_Count());
    printf("[DEBUG] Total Binary Value objects registered: %u\n", Binary_Value_Count());

    dlenv_init();
    atexit(datalink_cleanup);

    /* configure the timeout values */
    /* broadcast an I-Am on startup */
    Send_I_Am(&Handler_Transmit_Buffer[0]);

    ESP_LOGI(TAG, "BACnet demo with PMS5003 sensor started");
    ESP_LOGI(TAG, "PM1.0 available as Analog Value object instance %d", PM1_0_OBJECT_INSTANCE);
    ESP_LOGI(TAG, "PM2.5 available as Analog Value object instance %d", PM2_5_OBJECT_INSTANCE);
    ESP_LOGI(TAG, "PM10 available as Analog Value object instance %d", PM10_OBJECT_INSTANCE);
    ESP_LOGI(TAG, "PM2.5_SETPOINT available as Analog Value object instance %d (Default: 25.0 μg/m³)", PM2_5_SETPOINT_OBJECT_INSTANCE);
    ESP_LOGI(TAG, "FAN_STATUS available as Binary Input object instance %d", FAN_STATUS_OBJECT_INSTANCE);
    ESP_LOGI(TAG, "FAN_COMMAND available as Binary Output object instance %d", FAN_COMMAND_OBJECT_INSTANCE);
    ESP_LOGI(TAG, "SENSOR_ERROR available as Binary Value object instance %d", SENSOR_ERROR_OBJECT_INSTANCE);

    // Start the BACnet server listener task
    xTaskCreate(server_task, "bacnet_server", 8192, NULL, 1, NULL);
    ESP_LOGI(TAG, "Created BACnet server listener task");

    // Start the display task
    xTaskCreate(display_task, "display_task", 4096, NULL, 2, NULL);
    ESP_LOGI(TAG, "Created display task");
}

static object_functions_t Object_Table[] = {
    {
        OBJECT_DEVICE,
        NULL, /* don't init - recursive! */
        Device_Count,
        Device_Index_To_Instance,
        Device_Valid_Object_Instance_Number,
        Device_Object_Name,
        Device_Read_Property_Local,
        Device_Write_Property_Local,
        Device_Property_Lists,
        NULL,  // Object_RR_Info
        NULL,  // Object_Iterator
        NULL,  // Object_Value_List
        NULL,  // Object_COV
        NULL,  // Object_COV_Clear
        NULL   // Object_Intrinsic_Reporting
    },
    {
        OBJECT_ANALOG_VALUE,
        Analog_Value_Init,
        Analog_Value_Count,
        Analog_Value_Index_To_Instance,
        Analog_Value_Valid_Instance,
        Analog_Value_Object_Name,
        Analog_Value_Read_Property,
        Analog_Value_Write_Property,
        Analog_Value_Property_Lists,
        NULL,  // Object_RR_Info
        NULL,  // Object_Iterator
        NULL,  // Object_Value_List
        NULL,  // Object_COV
        NULL,  // Object_COV_Clear
        NULL   // Object_Intrinsic_Reporting
    },
    {
        OBJECT_BINARY_INPUT,
        Binary_Input_Init,
        Binary_Input_Count,
        Binary_Input_Index_To_Instance,
        Binary_Input_Valid_Instance,
        Binary_Input_Object_Name,
        Binary_Input_Read_Property,
        Binary_Input_Write_Property,
        Binary_Input_Property_Lists,
        NULL,  // Object_RR_Info
        NULL,  // Object_Iterator
        NULL,  // Object_Value_List
        NULL,  // Object_COV
        NULL,  // Object_COV_Clear
        NULL   // Object_Intrinsic_Reporting
    },
    {
        OBJECT_BINARY_OUTPUT,
        Binary_Output_Init,
        Binary_Output_Count,
        Binary_Output_Index_To_Instance,
        Binary_Output_Valid_Instance,
        Binary_Output_Object_Name,
        Binary_Output_Read_Property,
        Binary_Output_Write_Property,
        Binary_Output_Property_Lists,
        NULL,  // Object_RR_Info
        NULL,  // Object_Iterator
        NULL,  // Object_Value_List
        NULL,  // Object_COV
        NULL,  // Object_COV_Clear
        NULL   // Object_Intrinsic_Reporting
    },
    {
        OBJECT_BINARY_VALUE,
        Binary_Value_Init,
        Binary_Value_Count,
        Binary_Value_Index_To_Instance,
        Binary_Value_Valid_Instance,
        Binary_Value_Object_Name,
        Binary_Value_Read_Property,
        Binary_Value_Write_Property,
        Binary_Value_Property_Lists,
        NULL,  // Object_RR_Info
        NULL,  // Object_Iterator
        NULL,  // Object_Value_List
        NULL,  // Object_COV
        NULL,  // Object_COV_Clear
        NULL   // Object_Intrinsic_Reporting
    },
};

/** Initialize the handlers we will utilize.
 * @see Device_Init, apdu_set_unconfirmed_handler, apdu_set_confirmed_handler
 */
static void Init_Service_Handlers(void)
{
    Device_Init(&Object_Table[0]);
    /* we need to handle who-is to support dynamic device binding */
    apdu_set_unconfirmed_handler(SERVICE_UNCONFIRMED_WHO_IS, handler_who_is);
    apdu_set_unconfirmed_handler(SERVICE_UNCONFIRMED_WHO_HAS, handler_who_has);
    /* handle i-am to support binding to other devices */
    apdu_set_unconfirmed_handler(SERVICE_UNCONFIRMED_I_AM, handler_i_am_bind);
    /* set the handler for all the services we don't implement */
    /* It is required to send the proper reject message... */
    apdu_set_unrecognized_service_handler_handler(handler_unrecognized_service);
    /* Set the handlers for any confirmed services that we support. */
    /* We must implement read property - it's required! */
    apdu_set_confirmed_handler(SERVICE_CONFIRMED_READ_PROPERTY, handler_read_property);
    apdu_set_confirmed_handler(SERVICE_CONFIRMED_READ_PROP_MULTIPLE, handler_read_property_multiple);
    apdu_set_confirmed_handler(SERVICE_CONFIRMED_WRITE_PROPERTY, handler_write_property);
    apdu_set_confirmed_handler(SERVICE_CONFIRMED_WRITE_PROP_MULTIPLE, handler_write_property_multiple);
    apdu_set_confirmed_handler(SERVICE_CONFIRMED_READ_RANGE, handler_read_range);
    apdu_set_confirmed_handler(SERVICE_CONFIRMED_REINITIALIZE_DEVICE, handler_reinitialize_device);
    apdu_set_unconfirmed_handler(SERVICE_UNCONFIRMED_UTC_TIME_SYNCHRONIZATION, handler_timesync_utc);
    apdu_set_unconfirmed_handler(SERVICE_UNCONFIRMED_TIME_SYNCHRONIZATION, handler_timesync);
    apdu_set_confirmed_handler(SERVICE_CONFIRMED_SUBSCRIBE_COV, handler_cov_subscribe);
    apdu_set_unconfirmed_handler(SERVICE_UNCONFIRMED_COV_NOTIFICATION, handler_ucov_notification);
    /* handle communication so we can shutup when asked */
    apdu_set_confirmed_handler(SERVICE_CONFIRMED_DEVICE_COMMUNICATION_CONTROL, handler_device_communication_control);
    /* handle the data coming back from private requests */
    apdu_set_unconfirmed_handler(SERVICE_UNCONFIRMED_PRIVATE_TRANSFER, handler_unconfirmed_private_transfer);
}