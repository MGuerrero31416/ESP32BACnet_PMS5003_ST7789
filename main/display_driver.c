#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "display_driver.h"
#include "fonts.h"

static const char *TAG = "DISPLAY_DRIVER";

// ===========================================
// Display settings for Ideaspark 1.9"170x320
// ===========================================
#define LCD_HOST           SPI2_HOST
#define TFT_MOSI           23
#define TFT_SCLK           18
#define TFT_CS             15
#define TFT_DC             2
#define TFT_RST            4
#define TFT_BL             32
#define SPI_CLOCK_HZ       (10 * 1000 * 1000)
#define TFT_OFFSET_X 35
#define TFT_OFFSET_Y 0
#define DISPLAY_WIDTH  170
#define DISPLAY_HEIGHT 320 
#define SPI_CLOCK_HZ       (10 * 1000 * 1000)


/*
// ========================================
// display settings for TTGO 1.14" 135x240
// ========================================
#define LCD_HOST           SPI2_HOST
#define TFT_MOSI 19    
#define TFT_SCLK 18
#define TFT_CS   5
#define TFT_DC   16
#define TFT_RST  23   
#define TFT_BL   4     
#define TFT_OFFSET_X 52
#define TFT_OFFSET_Y 40
#define DISPLAY_WIDTH  170
#define DISPLAY_HEIGHT 320
#define SPI_CLOCK_HZ       (10 * 1000 * 1000)
*/

// Static variables
static esp_lcd_panel_handle_t panel_handle = NULL;
static esp_lcd_panel_io_handle_t io_handle = NULL;
static const font_t *current_font = &font_5x8;  // Default font

// COLOR TRANSFORMATION - FROM YOUR WORKING TESTS
static inline uint16_t display_color(uint16_t color) {
    switch (color & 0xFFFF) {
        case 0xF800: return 0xFFE0;  // Want Red → Send Yellow
        case 0x07E0: return 0xF81F;  // Want Green → Send Purple
        case 0x001F: return 0x07FF;  // Want Blue → Send Cyan
        case 0xFFE0: return 0xF800;  // Want Yellow → Send Red
        case 0xF81F: return 0x07E0;  // Want Purple → Send Green
        case 0x07FF: return 0x001F;  // Want Cyan → Send Blue
        case 0xFFFF: return 0x0000;  // Want White → Send Black
        case 0x0000: return 0xFFFF;  // Want Black → Send White
        default: return color;
    }
}

// ST7789 requires byte swap
static inline uint16_t swap_color_bytes(uint16_t color) {
    return (color << 8) | (color >> 8);
}

// Convert user color to display-ready color
static uint16_t color_to_display(uint16_t color) {
    return swap_color_bytes(display_color(color));
}

// Draw a character with specified font
static void draw_char_with_font(int x, int y, char c, uint16_t color, uint16_t bg_color, const font_t *font) {
    if (!panel_handle || !font) return;
    
    if (x < 0 || x >= DISPLAY_WIDTH - font->char_width || 
        y < 0 || y >= DISPLAY_HEIGHT - font->char_height) {
        return;
    }
    
    int index = 0;
    if (c >= font->start_char && c <= font->end_char) {
        index = (c - font->start_char) * font->bytes_per_char;
    } else {
        index = 0;  // Use first character (space) as fallback
    }
    
    uint16_t display_color_val = color_to_display(color);
    uint16_t display_bg_color = color_to_display(bg_color);
    
    // Draw character pixel by pixel
    // For simple fonts (like 5x8), each byte represents one column
    for (int fy = 0; fy < font->char_height; fy++) {
        for (int fx = 0; fx < font->char_width; fx++) {
            int pixel_x = x + fx;
            int pixel_y = y + fy;
            
            uint8_t pixel = 0;
            
            // Check if this pixel should be on based on font data
            // For 5x8 font, each column is 1 byte (8 bits high)
            if (fx < font->bytes_per_char) {
                uint8_t font_byte = font->data[index + fx];
                // For 8-bit high fonts, we check each bit
                // For taller fonts, we'd need more complex logic
                if (fy < 8) {
                    pixel = (font_byte >> fy) & 0x01;
                }
            }
            
            uint16_t pixel_color = pixel ? display_color_val : display_bg_color;
            
            // Only draw if not transparent background
            if (pixel_color != display_bg_color || bg_color != DISP_BLACK) {
                esp_lcd_panel_draw_bitmap(panel_handle, pixel_x, pixel_y, 
                                          pixel_x + 1, pixel_y + 1, &pixel_color);
            }
        }
    }
}

// ========== API IMPLEMENTATION ==========

int display_init(void) {
    ESP_LOGI(TAG, "Initializing TTGO T-Display");
    
    // 1. Initialize SPI bus
    spi_bus_config_t buscfg = {
        .sclk_io_num = TFT_SCLK,
        .mosi_io_num = TFT_MOSI,
        .miso_io_num = -1,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = DISPLAY_WIDTH * DISPLAY_HEIGHT * 2,
    };
    
    esp_err_t ret = spi_bus_initialize(LCD_HOST, &buscfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SPI bus initialization failed: %s", esp_err_to_name(ret));
        return -1;
    }
    
    // 2. Configure LCD panel IO
    esp_lcd_panel_io_spi_config_t io_config = {
        .dc_gpio_num = TFT_DC,
        .cs_gpio_num = TFT_CS,
        .pclk_hz = SPI_CLOCK_HZ,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .spi_mode = 3,
        .trans_queue_depth = 10,
        .on_color_trans_done = NULL,
        .user_ctx = NULL,
        .flags = {
            .dc_low_on_data = 0,
            .octal_mode = 0,
            .sio_mode = 0,
            .lsb_first = 0,
            .cs_high_active = 0,
        },
    };
    
    ret = esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST, &io_config, &io_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure LCD IO: %s", esp_err_to_name(ret));
        spi_bus_free(LCD_HOST);
        return -1;
    }
    
    // 3. Install ST7789 panel driver
    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = TFT_RST,
        .color_space = ESP_LCD_COLOR_SPACE_RGB,
        .bits_per_pixel = 16,
    };
    
    ret = esp_lcd_new_panel_st7789(io_handle, &panel_config, &panel_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to install ST7789 driver: %s", esp_err_to_name(ret));
        spi_bus_free(LCD_HOST);
        return -1;
    }
    
    // 4. Initialize display panel
    esp_lcd_panel_reset(panel_handle);
    esp_lcd_panel_init(panel_handle);
    
    // Set Memory Access Control (MADCTL) - BGR mode
    uint8_t madctl = 0x08;  // BGR MODE
    esp_lcd_panel_io_tx_param(io_handle, 0x36, &madctl, 1);
    vTaskDelay(pdMS_TO_TICKS(50));
    
    // Set Display offset
    esp_lcd_panel_set_gap(panel_handle, TFT_OFFSET_X, TFT_OFFSET_Y);

    // Turn on display
    esp_lcd_panel_disp_on_off(panel_handle, true);
    
    // 5. Enable backlight
    gpio_reset_pin(TFT_BL);
    gpio_set_direction(TFT_BL, GPIO_MODE_OUTPUT);
    gpio_set_level(TFT_BL, 1);
    
    ESP_LOGI(TAG, "Display initialized successfully");
    return 0;
}

void display_clear(uint16_t color) {
    if (!panel_handle) return;
    
    uint16_t display_color = color_to_display(color);
    uint16_t *line = malloc(DISPLAY_WIDTH * sizeof(uint16_t));
    if (!line) {
        ESP_LOGE(TAG, "Failed to allocate line buffer");
        return;
    }
    
    // Fill line buffer
    for (int x = 0; x < DISPLAY_WIDTH; x++) {
        line[x] = display_color;
    }
    
    // Draw line by line
    for (int y = 0; y < DISPLAY_HEIGHT; y++) {
        esp_lcd_panel_draw_bitmap(panel_handle, 0, y, DISPLAY_WIDTH, y + 1, line);
    }
    
    free(line);
}

// NEW: Draw string with specific font
void display_draw_string_font(int x, int y, const char *text, uint16_t color, uint16_t bg_color, const font_t *font) {
    if (!panel_handle || !text || !font) return;
    
    int current_x = x;
    while (*text) {
        draw_char_with_font(current_x, y, *text, color, bg_color, font);
        current_x += font->char_width + font->char_spacing;
        text++;
    }
}

// ORIGINAL: Draw string with current font
void display_draw_string(int x, int y, const char *text, uint16_t color, uint16_t bg_color) {
    display_draw_string_font(x, y, text, color, bg_color, current_font);
}

void display_fill_rect(int x, int y, int width, int height, uint16_t color) {
    if (!panel_handle) return;
    
    // Clamp coordinates
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x + width > DISPLAY_WIDTH) width = DISPLAY_WIDTH - x;
    if (y + height > DISPLAY_HEIGHT) height = DISPLAY_HEIGHT - y;
    if (width <= 0 || height <= 0) return;
    
    uint16_t display_color = color_to_display(color);
    uint16_t *line = malloc(width * sizeof(uint16_t));
    if (!line) {
        ESP_LOGE(TAG, "Failed to allocate line buffer");
        return;
    }
    
    // Fill line buffer
    for (int i = 0; i < width; i++) {
        line[i] = display_color;
    }
    
    // Draw line by line for the rectangle area
    for (int row = 0; row < height; row++) {
        esp_lcd_panel_draw_bitmap(panel_handle, x, y + row, x + width, y + row + 1, line);
    }
    
    free(line);
}

void display_set_backlight(int percent) {
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    
    // Simple on/off for now (could implement PWM)
    gpio_set_level(TFT_BL, percent > 0 ? 1 : 0);
}

int display_get_width(void) {
    return DISPLAY_WIDTH;
}

int display_get_height(void) {
    return DISPLAY_HEIGHT;
}

// NEW: Font management functions
void display_set_font(const font_t *font) {
    if (font) {
        current_font = font;
        ESP_LOGI(TAG, "Font set to %dx%d", font->char_width, font->char_height);
    }
}

const font_t* display_get_font(void) {
    return current_font;
}
