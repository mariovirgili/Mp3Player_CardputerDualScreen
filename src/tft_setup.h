#pragma once

// --- External driver 240x320 ---
#define ILI9341_DRIVER

// --- Use HSPI/SPI3 to keep separate from internal display ---
#define USE_HSPI_PORT

// --- External pins Cardputer-Adv ---
#define TFT_CS     5
#define TFT_DC     6
#define TFT_RST    3

// Moved to "alternative" GPIOs
#define TFT_SCLK 15
#define TFT_MOSI 13
#define TFT_MISO -1  // was 4

// Conservative frequencies (can be increased later)
#define SPI_FREQUENCY        10000000
#define SPI_READ_FREQUENCY  6000000

// No touch
#define TOUCH_CS -1

// If colors are weird, try uncommenting ONE of these:
#define TFT_RGB_ORDER TFT_BGR
// #define TFT_INVERSION_ON
#define TFT_INVERSION_OFF

// --- Fonts (CRITICAL for print/println and drawString) ---
#define LOAD_GLCD
#define LOAD_FONT2
#define LOAD_FONT4
#define LOAD_GFXFF
//#define SUPPORT_TRANSACTIONS
#define SMOOTH_FONT