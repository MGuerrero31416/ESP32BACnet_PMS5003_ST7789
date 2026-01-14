#ifndef FONTS_H
#define FONTS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Font structure - use the same definition style as display_driver.h
struct font_t {
    const uint8_t *data;       // Pointer to font data
    int char_width;           // Width of each character in pixels
    int char_height;          // Height of each character in pixels
    int char_spacing;         // Spacing between characters
    char start_char;          // First character in font (ASCII code)
    char end_char;            // Last character in font (ASCII code)
    int bytes_per_char;       // Bytes per character in data array
};

// This matches what display_driver.h expects
typedef struct font_t font_t;

// ==========================
// FONT DEFINITIONS
// ==========================

// 5x8 Font (Simple) - Original font
extern const font_t font_5x8;

// 8x8 Font (Wider) - Only numbers 0-9
extern const font_t font_8x8;

#ifdef __cplusplus
}
#endif

#endif // FONTS_H
