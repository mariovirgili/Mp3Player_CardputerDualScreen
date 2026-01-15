#pragma once

// --- Driver esterno 240x320 ---
#define ILI9341_DRIVER

// --- Usa HSPI/SPI3 per tenere separato dal display interno ---
#define USE_HSPI_PORT

// --- Pin esterni Cardputer-Adv ---
#define TFT_CS     5
#define TFT_DC     6
#define TFT_RST    3

// Spostati su GPIO "alternativi"
#define TFT_SCLK 15
#define TFT_MOSI 13
#define TFT_MISO -1  // was 4

// Frequenze conservative (poi puoi alzare)
#define SPI_FREQUENCY        10000000
#define SPI_READ_FREQUENCY  6000000

// Niente touch
#define TOUCH_CS -1

// Se i colori fossero strani, prova a decommentare UNA di queste:
#define TFT_RGB_ORDER TFT_BGR
// #define TFT_INVERSION_ON
#define TFT_INVERSION_OFF

// --- Fonts (CRITICO per print/println e drawString) ---
#define LOAD_GLCD
#define LOAD_FONT2
#define LOAD_FONT4
#define LOAD_GFXFF
//#define SUPPORT_TRANSACTIONS
#define SMOOTH_FONT