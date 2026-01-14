
/*static IP configuration for no-DHCP Metasys network*/
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "lwip/inet.h"       
#include "nvs_flash.h"
#include "wifi.h"

#define WIFI_SSID "BACnetBridge"        // Fixed the leading character issue
#define WIFI_PASS "@Pi31416"
#define MAXIMUM_RETRY 5

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static const char *TAG = "wifi";

EventGroupHandle_t wifi_event_group;

static int s_retry_num = 0;

static void event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_num < MAXIMUM_RETRY) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "retry to connect to the AP");
        } else {
            xEventGroupSetBits(wifi_event_group, WIFI_FAIL_BIT);
        }
        ESP_LOGI(TAG, "connect to the AP failed");
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

void wifi_initialize(void)
{
    wifi_event_group = xEventGroupCreate();

    esp_netif_create_default_wifi_sta();  // Creates the default STA interface

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_got_ip));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
            /* Optional: improve connection stability */
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
            .sae_pwe_h2e = WPA3_SAE_PWE_BOTH,
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "wifi_init_sta finished.");

    /* ------------------- STATIC IP CONFIGURATION ------------------- */
    esp_netif_t *sta_netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (sta_netif == NULL) {
        ESP_LOGE(TAG, "Failed to get STA netif handle");
    } else {
        ESP_ERROR_CHECK(esp_netif_dhcpc_stop(sta_netif));  // Disable DHCP

        esp_netif_ip_info_t ip_info = {0};

        ip_info.ip.addr      = htonl((10UL << 24) | (120UL << 16) | (245UL << 8)  | 92UL);   // 10.120.245.92
        ip_info.gw.addr      = htonl((10UL << 24) | (120UL << 16) | (245UL << 8)  | 254UL);  // 10.120.245.254
        ip_info.netmask.addr = htonl((255UL << 24) | (255UL << 16) | (255UL << 8) | 0UL);    // 255.255.255.0

        ESP_ERROR_CHECK(esp_netif_set_ip_info(sta_netif, &ip_info));

        ESP_LOGI(TAG, "Static IP assigned: 10.120.245.92");
    }
    /* --------------------------------------------------------------- */

    /* Waiting until either the connection is established or failed */
    EventBits_t bits = xEventGroupWaitBits(wifi_event_group,
            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
            pdFALSE,
            pdFALSE,
            portMAX_DELAY);

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "connected to ap SSID:%s (static IP: 10.120.245.92)", WIFI_SSID);
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGE(TAG, "Failed to connect to SSID:%s", WIFI_SSID);
    } else {
        ESP_LOGE(TAG, "UNEXPECTED EVENT");
    }
}