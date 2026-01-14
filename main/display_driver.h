#ifndef DISPLAY_DRIVER_H
#define DISPLAY_DRIVER_H

#include <stdint.h>

// Don't define font_t here, it's defined in fonts.h
// We'll include fonts.h in the .c files that need it

#ifdef __cplusplus
extern "C" {
#endif

// COLOR CONSTANTS
#define DISP_RED     0xF800
#define DISP_GREEN   0x07E0
#define DISP_BLUE    0x001F
#define DISP_WHITE   0xFFFF
#define DISP_BLACK   0x0000
#define DISP_YELLOW  0xFFE0
#define DISP_PURPLE  0xF81F
#define DISP_CYAN    0x07FF

// Forward declarations that use font_t
// We need to tell the compiler font_t exists
typedef struct font_t font_t;

// Function declarations
int display_init(void);
void display_clear(uint16_t color);
void display_draw_string(int x, int y, const char *text, uint16_t color, uint16_t bg_color);
void display_draw_string_font(int x, int y, const char *text, uint16_t color, uint16_t bg_color, const font_t *font);
void display_fill_rect(int x, int y, int width, int height, uint16_t color);
void display_set_backlight(int percent);
int display_get_width(void);
int display_get_height(void);

// Font management functions
void display_set_font(const font_t *font);
const font_t* display_get_font(void);

#ifdef __cplusplus
}
#endif

#endif // DISPLAY_DRIVER_H
