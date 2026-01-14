
idf_component_register(
    SRCS 
        "main.c"
        "wifi.c" 
        "pm25_sensor.c"
        "server_task.c"
        "display_driver.c"
        "display_task.c"
               
    INCLUDE_DIRS   
        "."
        "../components/fonts"
        REQUIRES
        nvs_flash freertos esp_wifi esp_event esp_timer bacnet driver esp_lcd log fonts
    
)

# Optimization for size
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Os")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Os")

