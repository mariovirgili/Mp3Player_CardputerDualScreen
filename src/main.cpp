#include <Arduino.h>
#include "Config.h"

// ===================== 1. FONT TRICK =====================
#include <TFT_eSPI.h> 
typedef GFXfont TFT_Native_Font; 

namespace MyFonts {
  #include <Fonts/GFXFF/FreeSansBold9pt7b.h>
  #include <Fonts/GFXFF/FreeSansBold12pt7b.h>
}

// ===================== 2. LIBRARIES =====================
#ifdef SPI_MOSI_DLEN_REG
  #undef SPI_MOSI_DLEN_REG
#endif

#include <SPI.h>
#include <M5Cardputer.h> 
#include <AudioFileSourceSD.h>
#include <AudioFileSourceID3.h>
#include <AudioGeneratorMP3.h>
#include <AudioGeneratorFLAC.h>
#include <AudioOutput.h>

#include "FileManager.h"
#include "Logo.h" 
#include "Buttons.h" 

#ifndef KEY_ESC
#define KEY_ESC 27
#endif

FileManager FM;

// ===================== DISPLAY OBJECTS =====================
TFT_eSPI externalDisplay = TFT_eSPI();
TFT_eSprite activeSprite = TFT_eSprite(&externalDisplay); 
TFT_eSprite buttonsLayer = TFT_eSprite(&externalDisplay); 

// ===================== GLOBAL VARIABLES =====================
unsigned short grays[18];
int cursorIdx = 0;
int playingCursorIdx = 0; 
int volume = 12; 
// IMPORTANT: sampleRate is volatile because it's modified by Core 0 and read by Core 1
volatile int sampleRate = 44100; 
int bitRate = 0; 
bool isVBR = false; 
int detectedBaseBitrate = 0;

enum PlayerState { STATE_STOPPED, STATE_PLAYING, STATE_PAUSED };
PlayerState currentState = STATE_STOPPED;

// CONTROL FLAGS
bool nextS = false;
bool volUp = false; // Flag to indicate volume changed (up or down) to update UI
bool showHelp = false; 
bool helpRedrawNeeded = false; 
bool forceFullRedraw = true; 
bool needInternalRedraw = true;
volatile bool isLoading = false; 

unsigned long lastInputTime = 0;
bool isScreenOn = true;
const unsigned long SCREEN_TIMEOUT = 10000; 
const uint8_t SCREEN_BRIGHTNESS_ON = 100;   

unsigned long trackStartTime = 0;
unsigned long pausedAt = 0; 
uint32_t trackDurationSec = 0;
String activeFilePath = "", currentCodec = "NONE";

static int vuL = 0, vuR = 0;
static int marqueeX = 300; 

#if !defined(USE_ILI9488)
  static int marqueeW = 0;
  static unsigned long lastMarqueeMs = 0;
#endif

static unsigned long lastBattMs = 0;
static int battPct = 0;
static float battVolts = 0.0f; 

// ===================== AUDIO CLASS =====================
class AudioOutputM5CardputerSpeaker : public AudioOutput {
public:
  AudioOutputM5CardputerSpeaker(m5::Speaker_Class* m5sound) { _m5sound = m5sound; }
  
  virtual bool begin() override { return true; }
  
  // FIX: Intercept file frequency change (e.g. FLAC 48kHz vs MP3 44.1kHz)
  virtual bool SetRate(int hz) override {
      sampleRate = hz; 
      return true;
  }

  virtual bool ConsumeSample(int16_t sample[2]) override {
    int16_t l = abs(sample[0]);
    int16_t r = abs(sample[1]);
    if (l > peakL) peakL = l;
    if (r > peakR) peakR = r;

    if (_tri_buffer_index < 1536) {
      _tri_buffer[_tri_index][_tri_buffer_index++] = (int16_t)(((int32_t)sample[0] + sample[1]) / 2);
      return true;
    }
    flush(); return false;
  }
  
  virtual void flush() override {
    if (_tri_buffer_index > 0) {
      while (_m5sound->isPlaying()) vTaskDelay(1);
      // Use the actual sampleRate updated by SetRate
      _m5sound->playRaw(_tri_buffer[_tri_index], _tri_buffer_index, sampleRate, false);
      _tri_index = (_tri_index + 1) % 3;
      _tri_buffer_index = 0;
    }
  }
  virtual bool stop() override { flush(); while (_m5sound->isPlaying()) vTaskDelay(1); return true; }
  int16_t peakL = 0, peakR = 0;
private:
  m5::Speaker_Class* _m5sound;
  int16_t _tri_buffer[3][1536];
  size_t _tri_buffer_index = 0, _tri_index = 0;
};

AudioGenerator* audioGen = nullptr;
AudioFileSourceSD* audioFile = nullptr;
AudioFileSourceID3* audioId3 = nullptr;
AudioOutputM5CardputerSpeaker* audioOut = nullptr;

// ===================== UTILS =====================
static String timeMMSS(unsigned long s) { 
  char b[12]; snprintf(b, sizeof(b), "%02lu:%02lu", s/60, s%60); 
  return String(b); 
}

static String baseName(const String& p) { 
  int s = p.lastIndexOf('/'); 
  return (s >= 0) ? p.substring(s + 1) : p; 
}

// ===================== SPLASH SCREEN =====================
void showSplashScreen() {
  M5Cardputer.Display.setBrightness(SCREEN_BRIGHTNESS_ON);
  M5Cardputer.Display.fillScreen(TFT_BLACK);
  int ix = (M5Cardputer.Display.width() - internalLogoW) / 2;
  int iy = (M5Cardputer.Display.height() - internalLogoH) / 2;
  M5Cardputer.Display.setSwapBytes(true);
  M5Cardputer.Display.pushImage(ix, iy, internalLogoW, internalLogoH, logoInternal);
  
  externalDisplay.fillScreen(TFT_BLACK);
  int ex = (externalDisplay.width() - externalLogoW) / 2;
  int ey = (externalDisplay.height() - externalLogoH) / 2; 
  externalDisplay.setSwapBytes(true);
  externalDisplay.pushImage(ex, ey, externalLogoW, externalLogoH, logoExternal);
  
  M5Cardputer.Display.setSwapBytes(false);
  externalDisplay.setSwapBytes(false);
}

// ===================== UI FUNCTIONS =====================
void drawInternalBrowser() {
  if (!isScreenOn) return;
  M5Cardputer.Display.fillScreen(TFT_BLACK);
  M5Cardputer.Display.setTextSize(1);
  M5Cardputer.Display.setTextDatum(TC_DATUM); 
  
  M5Cardputer.Display.setTextColor(TFT_ORANGE);
  M5Cardputer.Display.drawString("Mp3 Player v0.2.1 - Browser", 120, 5); 
  M5Cardputer.Display.setTextColor(TFT_LIGHTGREY);
  M5Cardputer.Display.drawString("H: Help | P: Play | S: Stop", 120, 15);
  M5Cardputer.Display.drawFastHLine(0, 26, 240, TFT_DARKGREY);
  M5Cardputer.Display.setTextDatum(TL_DATUM); 
  
  int count = FM.getCount();
  for (int i = 0; i < 6; i++) {
    int idx = max(0, cursorIdx - 2) + i; 
    if (idx >= count) break;
    FileItem item = FM.getItem(idx);
    M5Cardputer.Display.setCursor(5, 30 + (i * 15));
    if (idx == cursorIdx) {
      M5Cardputer.Display.setTextColor(TFT_BLACK, TFT_WHITE);
      M5Cardputer.Display.print(" > ");
    } else {
      uint16_t itemColor = TFT_WHITE;
      if (item.isDir) itemColor = TFT_CYAN;
      else {
        String n = item.name;
        n.toLowerCase();
        if (n.endsWith(".flac")) itemColor = TFT_YELLOW;      
        else if (n.endsWith(".mp3")) itemColor = TFT_GREEN;   
        else itemColor = TFT_LIGHTGREY;
      }
      M5Cardputer.Display.setTextColor(itemColor, TFT_BLACK);
      M5Cardputer.Display.print(item.isDir ? " [D] " : "     ");
    }
    M5Cardputer.Display.println(item.name.substring(0, 24));
  }
}

void drawInternalHelp() {
  if (!isScreenOn) return;
  M5Cardputer.Display.fillScreen(TFT_BLACK);
  M5Cardputer.Display.drawRect(0, 0, 240, 135, TFT_BLUE);
  M5Cardputer.Display.drawRect(3, 3, 234, 129, TFT_WHITE);
  M5Cardputer.Display.setTextSize(1);
  M5Cardputer.Display.setTextDatum(TC_DATUM);
  M5Cardputer.Display.setTextColor(TFT_YELLOW);
  M5Cardputer.Display.drawString("KEYBOARD CONTROLS", 120, 10);
  M5Cardputer.Display.drawFastHLine(10, 22, 220, TFT_BLUE);
  M5Cardputer.Display.setTextDatum(TL_DATUM);
  M5Cardputer.Display.setTextColor(TFT_WHITE);
  int y = 30; int x = 20; int spacing = 14; 
  // UPDATED HELP TEXT (Display + / - as requested)
  M5Cardputer.Display.drawString("P       : Play / Pause", x, y); y += spacing;
  M5Cardputer.Display.drawString("S       : Stop", x, y); y += spacing;
  M5Cardputer.Display.drawString("[ / ]   : Prev / Next Track", x, y); y += spacing;
  M5Cardputer.Display.drawString("+ / -   : Volume Up / Down", x, y); y += spacing;
  M5Cardputer.Display.drawString("; / .   : Nav Up / Down", x, y); y += spacing;
  M5Cardputer.Display.drawString("ENTER   : Select / Open", x, y); y += spacing;
  M5Cardputer.Display.setTextDatum(TC_DATUM);
  M5Cardputer.Display.setTextColor(TFT_RED);
  M5Cardputer.Display.drawString("H / ESC : CLOSE HELP", 120, 118);
}

void drawHelpScreen() {
  int W = externalDisplay.width();
  int H = externalDisplay.height();
  externalDisplay.fillScreen(TFT_BLACK);
  externalDisplay.drawRect(10, 10, W-20, H-20, TFT_BLUE);
  externalDisplay.drawRect(14, 14, W-28, H-28, TFT_WHITE);
  externalDisplay.setFreeFont((const TFT_Native_Font*)&MyFonts::FreeSansBold12pt7b);
  externalDisplay.setTextColor(TFT_YELLOW, TFT_BLACK);
  externalDisplay.setTextDatum(TC_DATUM);
  externalDisplay.drawString("COMMANDS LIST", W/2, 25);
  externalDisplay.setFreeFont((const TFT_Native_Font*)&MyFonts::FreeSansBold9pt7b);
  externalDisplay.setTextDatum(TL_DATUM);
  int yStart = 55; int lineH = 25; int colKey = 40; int colDesc = 120;
  
  auto drawLine = [&](String key, String desc) {
      externalDisplay.setTextColor(TFT_CYAN, TFT_BLACK);
      externalDisplay.drawString(key, colKey, yStart); 
      externalDisplay.setTextColor(TFT_WHITE, TFT_BLACK); 
      externalDisplay.drawString(desc, colDesc, yStart);
      yStart += lineH;
  };

  // UPDATED HELP TEXT FOR NEW LAYOUT
  drawLine("P", "Play / Pause");
  drawLine("S", "Stop Playback");
  drawLine("[ / ]", "Prev / Next Track");
  drawLine("+ / -", "Volume Up / Down");
  drawLine("; / .", "Nav Up / Down");
  drawLine("ENTER", "Select / Open");
  
  externalDisplay.setTextDatum(TC_DATUM);
  externalDisplay.setTextColor(TFT_RED, TFT_BLACK);
  externalDisplay.setFreeFont(NULL); externalDisplay.setTextFont(2); externalDisplay.setTextSize(1);
  externalDisplay.drawString("PRESS [H] or [ESC] TO CLOSE", W/2, H-35); 
}

static void buildButtonsLayer(uint16_t bgColor) {
  int W = externalDisplay.width();
  if (!buttonsLayer.created() || buttonsLayer.width() != W) {
      buttonsLayer.deleteSprite();
      buttonsLayer.createSprite(W, btnH + 10);
  }
  buttonsLayer.fillSprite(bgColor);
  
  int totalBtnWidth = 5 * btnW;
  int gap = (W - totalBtnWidth) / 6; 
  if (gap < 2) gap = 2;

  for (int i = 0; i < 5; i++) {
    int x = gap + (i * (btnW + gap)); 
    const unsigned short* imgPtr = nullptr;
    switch(i) {
      case 0: imgPtr = btnPrev; break;
      case 1: imgPtr = (currentState == STATE_PLAYING) ? btnPlayOn : btnPlayOff; break;
      case 2: imgPtr = (currentState == STATE_PAUSED) ? btnPauseOn : btnPauseOff; break;
      case 3: imgPtr = (currentState == STATE_STOPPED) ? btnStopOn : btnStopOff; break;
      case 4: imgPtr = btnNext; break;
    }
    if (imgPtr != nullptr) {
      buttonsLayer.setSwapBytes(true);
      buttonsLayer.pushImage(x, 3, btnW, btnH, imgPtr);
      buttonsLayer.setSwapBytes(false);
    }
  }
}

// === DUAL LAYOUT DRAWING SYSTEM (FLICKER FREE) ===
void drawWinampExternal() {
  int W = externalDisplay.width();
  int H = externalDisplay.height();

  static String lastCodecDrawn = "";
  static int lastVolDrawn = -1;
  static int lastBattDrawn = -1;
  static PlayerState lastBtnState = (PlayerState)-1; 
  
  if (millis() - lastBattMs > 2000) {
    lastBattMs = millis();
    battPct = M5Cardputer.Power.getBatteryLevel();
    int voltageMv = M5Cardputer.Power.getBatteryVoltage();
    battVolts = voltageMv / 1000.0; 
  }

  // --- 1. HEADER (OPTIMIZED) ---
  int headerH = titleLogoH + 4; 
  
  if (forceFullRedraw || currentCodec != lastCodecDrawn) {
      uint16_t titleBarColor;
      #if defined(USE_ILI9488)
        titleBarColor = TFT_BLACK; 
      #else
        titleBarColor = externalDisplay.color565(50, 50, 50); 
      #endif
      externalDisplay.fillRect(0, 0, W, headerH + 2, titleBarColor);
      
      int logoY = 2 + ((headerH - titleLogoH) / 2); 
      externalDisplay.setSwapBytes(true); 
      externalDisplay.pushImage(6, logoY, titleLogoW, titleLogoH, logoTitleBar);
      externalDisplay.setSwapBytes(false); 
    
      const unsigned short* codecImgPtr = nullptr;
      int codecW = 0;
      if (currentState == STATE_STOPPED && currentCodec == "NONE") { codecImgPtr = imgCodecNone; codecW = codecNoneW; }
      else if (currentCodec == "MP3")    { codecImgPtr = imgCodecMp3;  codecW = codecAudioW; }
      else if (currentCodec == "FLAC")   { codecImgPtr = imgCodecFlac; codecW = codecAudioW; } 
      else                               { codecImgPtr = imgCodecNone; codecW = codecNoneW; }
    
      if (codecImgPtr != nullptr) {
        int xPos = W - codecW - 6; 
        int currentCodecH = 32; 
        int codecY = 2; 
        
        #if defined(USE_ILI9488)
           currentCodecH = 50; 
           codecY = 0; 
        #else
           codecY = 2 + ((headerH - currentCodecH) / 2);
        #endif
        
        externalDisplay.setSwapBytes(true);
        externalDisplay.pushImage(xPos, codecY, codecW, currentCodecH, codecImgPtr);
        externalDisplay.setSwapBytes(false);
      }
      lastCodecDrawn = currentCodec;
  }

  // --- 2. MIDDLE SECTION ---
  int buttonsMargin = 5;
  #if defined(USE_ILI9488)
     buttonsMargin = 15; 
  #endif
  
  int buttonsY = H - (btnH + buttonsMargin);
  int bottomBarY = buttonsY - 12;
  int middleStartY = headerH + 2;
  int middleHeight = bottomBarY - middleStartY; 

  if (!activeSprite.created() || activeSprite.width() != W || activeSprite.height() != middleHeight) {
      activeSprite.deleteSprite();
      activeSprite.createSprite(W, middleHeight);
  }

  const uint16_t bgPanel = externalDisplay.color565(57, 52, 33);
  activeSprite.fillSprite(bgPanel); 

  int blackAreaY, blackAreaH, meterHeight;
  int textLine1Y, textLine2Y;
  int nextLineY, progressBarY, marqueeY;
  int progBarH = 6;     
  int progBarFillH = 4; 
  int marqueeH = 38;

  #if defined(USE_ILI9488)
      // >>> 480x320 TUNED LAYOUT <<<
      blackAreaY = 15; 
      blackAreaH = 55; 
      meterHeight = blackAreaH - 12;
      textLine1Y = blackAreaY + 8;
      textLine2Y = blackAreaY + 28;
      nextLineY = blackAreaY + blackAreaH + 20; 
      progressBarY = nextLineY + 25;
      progBarH = 12;      
      progBarFillH = 10;  
      marqueeY = progressBarY + 15; 
      marqueeH = 34; // FIX 9488: Height Reduced to 34px (User Request)
  #else
      // >>> 320x240 ORIGINAL LAYOUT <<<
      blackAreaY = 2; 
      blackAreaH = 100; 
      meterHeight = 90; 
      textLine1Y = blackAreaY + 12;
      textLine2Y = blackAreaY + 32;
      nextLineY = blackAreaY + 57;
      progressBarY = blackAreaY + 104; 
      marqueeY = progressBarY + 10;
      marqueeH = 36;
  #endif

  // Draw Elements
  activeSprite.fillRect(10, blackAreaY, W - 20, blackAreaH, TFT_BLACK);
  
  if (currentState == STATE_PLAYING && audioOut) {
    vuL = constrain(map(audioOut->peakL, 0, 32000, 0, meterHeight), 0, meterHeight); 
    audioOut->peakL = 0; 
    vuR = constrain(map(audioOut->peakR, 0, 32000, 0, meterHeight), 0, meterHeight); 
    audioOut->peakR = 0; 
  } else { vuL = 0; vuR = 0; }
  
  auto drawVU = [&](int x, int val) {
    int yOff = 4; 
    #if defined(USE_ILI9488)
       yOff = 7;
    #endif
    activeSprite.drawRect(x, blackAreaY + yOff, 20, meterHeight, grays[6]); 
    int step = 4;
    #if defined(USE_ILI9488)
       step = 3;
    #endif
    for(int i=0; i<val; i+=step) { 
      uint16_t c = (i < (meterHeight*0.6)) ? TFT_GREEN : (i < (meterHeight*0.85) ? TFT_YELLOW : TFT_RED);
      int barY = (blackAreaY + yOff + meterHeight - 5) - i;
      if(barY >= blackAreaY + yOff) activeSprite.fillRect(x+2, barY, 16, step-1, c); 
    }
  };
  drawVU(20, vuL); 
  drawVU(45, vuR); 

  activeSprite.setFreeFont(NULL);
  activeSprite.setTextFont(2); 
  activeSprite.setTextColor(TFT_WHITE, TFT_BLACK); 
  activeSprite.setTextDatum(TL_DATUM); 
  
  String line1 = "Bitrate: " + (isVBR ? "VBR" : (String(bitRate/1000) + " kbps"));
  String line2 = "Freq: " + String((float)sampleRate/1000.0, 1) + " kHz";

  activeSprite.drawString(line1, 75, textLine1Y, 2); 
  activeSprite.drawString(line2, 75, textLine2Y, 2);

  unsigned long elapsed = 0;
  if (currentState == STATE_PLAYING) elapsed = (millis() - trackStartTime) / 1000;
  else if (currentState == STATE_PAUSED) elapsed = (pausedAt - trackStartTime) / 1000;
  
  activeSprite.setTextFont(4); 
  activeSprite.setTextColor(TFT_GREEN, TFT_BLACK); 
  activeSprite.setTextDatum(TR_DATUM); 
  activeSprite.drawString(timeMMSS(elapsed), W - 20, textLine1Y); 
  
  activeSprite.setTextFont(2); 
  activeSprite.setTextColor(TFT_WHITE, TFT_BLACK);
  String totalTime = (trackDurationSec > 0) ? timeMMSS(trackDurationSec) : "--:--";
  int timerSlashY = textLine1Y + 27; 
  activeSprite.drawString("/ " + totalTime, W - 20, timerSlashY);

  activeSprite.setTextDatum(TL_DATUM);
  activeSprite.setFreeFont(NULL);
  
  #if defined(USE_ILI9488)
     activeSprite.drawFastHLine(10, nextLineY, W - 20, grays[6]);
  #else
     activeSprite.drawFastHLine(85, nextLineY, W - 110, grays[6]);
  #endif

  #if defined(USE_ILI9488)
     activeSprite.setTextColor(TFT_ORANGE, bgPanel); 
  #else
     activeSprite.setTextColor(TFT_ORANGE, TFT_BLACK); 
  #endif

  activeSprite.setTextFont(1); 
  #if defined(USE_ILI9488)
     activeSprite.drawString("NEXT:", 10, nextLineY + 5); 
  #else
     activeSprite.drawString("NEXT:", 85, nextLineY + 5);
  #endif
  
  int nextIdxDummy;
  String nextTrackPath = FM.getNextAudio(playingCursorIdx, 1, nextIdxDummy);
  String nextTrackName = (nextTrackPath != "") ? baseName(nextTrackPath) : "---";
  
  activeSprite.setFreeFont((const TFT_Native_Font*)&MyFonts::FreeSansBold9pt7b);
  
  #if defined(USE_ILI9488)
     activeSprite.setTextColor(TFT_WHITE, bgPanel);
  #else
     activeSprite.setTextColor(TFT_WHITE, TFT_BLACK); 
  #endif

  if (nextTrackName.length() > 30) nextTrackName = nextTrackName.substring(0, 28) + "..";
  #if !defined(USE_ILI9488)
     if (nextTrackName.length() > 18) nextTrackName = nextTrackName.substring(0, 16) + "..";
  #endif
  
  #if defined(USE_ILI9488)
     activeSprite.drawString(nextTrackName, 50, nextLineY + 2);
  #else
     activeSprite.drawString(nextTrackName, 85, nextLineY + 12); 
  #endif

  activeSprite.drawRect(10, progressBarY, W - 20, progBarH, grays[6]);
  if (currentState != STATE_STOPPED && trackDurationSec > 0) {
    float p = (float)elapsed / trackDurationSec;
    activeSprite.fillRect(11, progressBarY + 1, (int)(min(1.0f, p) * (W - 22)), progBarFillH, TFT_GOLD);
  }

  activeSprite.fillRect(10, marqueeY, W - 20, marqueeH, TFT_BLACK);
  activeSprite.drawRect(10, marqueeY, W - 20, marqueeH, grays[8]);
  activeSprite.drawFastHLine(10, marqueeY + marqueeH - 1, W - 20, grays[8]);

  String title = (activeFilePath != "") ? baseName(activeFilePath) : "READY";
  uint16_t codecColor = (currentCodec == "FLAC") ? TFT_CYAN : TFT_GOLD;

  #if defined(USE_ILI9488)
      // === STATIC TITLE FOR 9488 ===
      activeSprite.setFreeFont((const TFT_Native_Font*)&MyFonts::FreeSansBold12pt7b); 
      activeSprite.setTextColor(codecColor, TFT_BLACK); 
      if (title.length() > 28) title = title.substring(0, 26) + "..";
      // Adjusted center for taller box
      activeSprite.drawString(title, 15, marqueeY + 8); 
  #else
      // === SCROLLING MARQUEE FOR 9341 ===
      activeSprite.setViewport(11, marqueeY + 1, W - 22, 36);
      activeSprite.setFreeFont((const TFT_Native_Font*)&MyFonts::FreeSansBold12pt7b); 
      marqueeW = activeSprite.textWidth(title);
      if (millis() - lastMarqueeMs >= 35) { 
        lastMarqueeMs = millis(); 
        marqueeX -= 3; 
        if (marqueeX < -marqueeW) marqueeX = W - 20; 
      }
      activeSprite.setTextColor(codecColor, TFT_BLACK); 
      activeSprite.drawString(title, marqueeX, 6);
      activeSprite.resetViewport();
  #endif

  activeSprite.pushSprite(0, middleStartY);

  // --- 3. FOOTER ---
  uint16_t footerCol = bgPanel;
  #if defined(USE_ILI9488)
     footerCol = TFT_BLACK;
  #endif

  if (forceFullRedraw) {
      externalDisplay.fillRect(0, bottomBarY, W, 12, footerCol);
      externalDisplay.setFreeFont(NULL);
      externalDisplay.setTextSize(1);
      externalDisplay.drawRect(10, bottomBarY + 2, 16, 8, TFT_LIGHTGREY); 
      externalDisplay.drawRect(110, bottomBarY + 2, W - 120, 8, grays[4]);
      
      lastBattDrawn = -1;
      lastVolDrawn = -1;
      
      // FIX: Force redraw of buttons when returning from Help/Menu
      lastBtnState = (PlayerState)-1; 
      
      forceFullRedraw = false;
  }

  if (battPct != lastBattDrawn) {
      uint16_t battCol = (battPct > 20) ? TFT_GREEN : TFT_RED;
      externalDisplay.fillRect(11, bottomBarY + 3, 14, 6, footerCol); 
      externalDisplay.fillRect(11, bottomBarY + 3, (14 * battPct)/100, 6, battCol);
      externalDisplay.setTextColor(TFT_WHITE, footerCol); 
      externalDisplay.setCursor(32, bottomBarY + 3); 
      externalDisplay.printf("%d%% %.2fV", battPct, battVolts);
      lastBattDrawn = battPct;
  }

  if (volume != lastVolDrawn) {
      int volBarX = 110;
      int volBarW = W - 120;
      externalDisplay.fillRect(volBarX + 1, bottomBarY + 3, volBarW - 2, 6, footerCol);
      int volFill = map(volume, 0, 21, 0, volBarW - 2); 
      volFill = constrain(volFill, 0, volBarW - 2);    
      externalDisplay.fillRect(volBarX + 1, bottomBarY + 3, volFill, 6, TFT_CYAN);
      lastVolDrawn = volume;
  }

  // Redraw buttons only if state changes
  if (currentState != lastBtnState) {
      buildButtonsLayer(footerCol); 
      buttonsLayer.pushSprite(0, buttonsY);
      lastBtnState = currentState;
  }
}

// ===================== AUDIO LOGIC TASK =====================
void Task_Audio(void *p) {
  while (1) {
    // FIX: SMART YIELD LOGIC REMOVED FROM HERE, MOVED TO MAIN LOOP DELAY
    if (volUp) { M5Cardputer.Speaker.setVolume(map(volume, 0, 21, 0, 255)); volUp = false; }
    
    if (nextS) {
      String pathLower = activeFilePath;
      pathLower.toLowerCase();

      #if defined(USE_ILI9488)
         if (pathLower.endsWith(".flac")) {
             isLoading = true; 
         }
      #endif
      
      if (audioGen) { audioGen->stop(); delete audioGen; audioGen = nullptr; }
      if (audioId3) { delete audioId3; audioId3 = nullptr; }
      if (audioFile) { delete audioFile; audioFile = nullptr; }

      audioFile = new AudioFileSourceSD(activeFilePath.c_str());
      uint32_t fsize = audioFile->getSize();
      sampleRate = 44100; 
      isVBR = false;      

      if (pathLower.endsWith(".flac")) { 
        audioGen = new AudioGeneratorFLAC(); 
        currentCodec = "FLAC";
        bitRate = 850000; 
        trackDurationSec = (fsize * 8) / bitRate; 
        
        M5Cardputer.Speaker.setVolume(map(volume, 0, 21, 0, 255));
        audioOut->SetGain(1.0); 
        
        audioGen->begin(audioFile, audioOut);
        
        #if defined(USE_ILI9488)
           unsigned long safety = millis();
           while (millis() - safety < 800) { 
               if (audioGen->isRunning()) audioGen->loop();
           }
        #endif
      } 
      else {
        audioGen = new AudioGeneratorMP3(); 
        currentCodec = "MP3";
        bitRate = 128000; 
        trackDurationSec = (fsize * 8) / bitRate; 
        audioId3 = new AudioFileSourceID3(audioFile);
        audioGen->begin(audioId3, audioOut);
      }
      
      trackStartTime = millis(); 
      currentState = STATE_PLAYING; 
      nextS = false; 
      
      if (isLoading) {
         isLoading = false; 
         forceFullRedraw = true;
      }
      
      marqueeX = 300;
    }
    
    if (currentState == STATE_PLAYING && audioGen && audioGen->isRunning()) {
      if (!audioGen->loop()) { 
        currentState = STATE_STOPPED; 
      } 
    }
    
    // FIX: "Smart Yield" Logic
    if (currentState != STATE_PLAYING) {
       vTaskDelay(1); // Sleep if idle
    } else {
       static int loopCounter = 0;
       loopCounter++;
       if (loopCounter > 3) { // Yield only 1 every 4 cycles to breathe but not starve
          vTaskDelay(1);
          loopCounter = 0;
       }
    }
  }
}

// ===================== SETUP & LOOP =====================
void setup() {
  auto cfg = M5.config(); M5Cardputer.begin(cfg);
  pinMode(3, OUTPUT); digitalWrite(3, LOW); delay(100); digitalWrite(3, HIGH);
  
  externalDisplay.begin(); 
  externalDisplay.setRotation(3);
  int co = 220; for (int i = 0; i < 18; i++) { grays[i] = externalDisplay.color565(co, co, co); co -= 11; }
  
  showSplashScreen();
  lastInputTime = millis();

  SPI.begin(SD_SCK_GPIO, SD_MISO_GPIO, SD_MOSI_GPIO, SD_CS_GPIO);
  FM.begin(); 
  
  M5Cardputer.Speaker.begin();
  M5Cardputer.Speaker.setVolume(map(volume, 0, 21, 0, 255)); 
  
  if (SD.exists("/player/intro.mp3")) {
      AudioFileSourceSD *jingleFile = new AudioFileSourceSD("/player/intro.mp3");
      AudioGeneratorMP3 *jingleGen = new AudioGeneratorMP3();
      AudioOutputM5CardputerSpeaker *jingleOut = new AudioOutputM5CardputerSpeaker(&M5Cardputer.Speaker);
      jingleGen->begin(new AudioFileSourceID3(jingleFile), jingleOut);
      while(jingleGen->isRunning()) { if (!jingleGen->loop()) jingleGen->stop(); }
      delete jingleGen; delete jingleFile; delete jingleOut;
  } else { delay(2000); }

  audioOut = new AudioOutputM5CardputerSpeaker(&M5Cardputer.Speaker);
  
  drawInternalBrowser();
  needInternalRedraw = false; 

  // FIX: Move Task to Core 0 (Separate from UI on Core 1)
  xTaskCreatePinnedToCore(Task_Audio, "Audio", 29000, NULL, 3, NULL, 0);
}

void loop() {
  M5Cardputer.update();
  if (isScreenOn && (millis() - lastInputTime > SCREEN_TIMEOUT)) {
      M5Cardputer.Display.setBrightness(0);
      isScreenOn = false;
  }

  if (M5Cardputer.Keyboard.isChange()) {
    lastInputTime = millis();
    if (!isScreenOn) {
        M5Cardputer.Display.setBrightness(SCREEN_BRIGHTNESS_ON);
        isScreenOn = true;
    }

    if (M5Cardputer.Keyboard.isKeyPressed(KEY_ESC) || M5Cardputer.Keyboard.isKeyPressed('`') || M5Cardputer.Keyboard.isKeyPressed('h')) {
      showHelp = !showHelp;
      if (showHelp) {
        helpRedrawNeeded = true;
      } else {
        needInternalRedraw = true; 
        forceFullRedraw = true; 
      }
    }

    if (!showHelp) {
      // NAVIGATION (Semicolon / Dot)
      if (M5Cardputer.Keyboard.isKeyPressed(';')) { cursorIdx = (cursorIdx - 1 + FM.getCount()) % FM.getCount(); needInternalRedraw = true; }
      if (M5Cardputer.Keyboard.isKeyPressed('.')) { cursorIdx = (cursorIdx + 1) % FM.getCount(); needInternalRedraw = true; }
      
      // SELECT (Enter)
      if (M5Cardputer.Keyboard.isKeyPressed(13) || M5Cardputer.Keyboard.isKeyPressed(10) || M5Cardputer.Keyboard.isKeyPressed(KEY_ENTER)) {
        String newFile;
        if (FM.handleSelection(cursorIdx, newFile)) {
          activeFilePath = newFile; playingCursorIdx = cursorIdx; nextS = true; 
        } else { cursorIdx = 0; needInternalRedraw = true; }
      }

      // PLAY / PAUSE (Changed to 'p')
      if (M5Cardputer.Keyboard.isKeyPressed('p')) { 
        if (currentState == STATE_PLAYING) { currentState = STATE_PAUSED; pausedAt = millis(); } 
        else if (currentState == STATE_PAUSED) { currentState = STATE_PLAYING; trackStartTime += (millis() - pausedAt); pausedAt = 0; }
        else if (currentState == STATE_STOPPED) { 
            // PLAY SELECTION FROM BROWSER IF STOPPED
            String newFile;
            if (FM.handleSelection(cursorIdx, newFile)) {
               activeFilePath = newFile; playingCursorIdx = cursorIdx; nextS = true;
            }
        }
      }

      // STOP (Remains 's')
      if (M5Cardputer.Keyboard.isKeyPressed('s')) { if (audioGen) audioGen->stop(); currentState = STATE_STOPPED; trackStartTime = 0; }

      // VOLUME UP ('=' mapped to +)
      if (M5Cardputer.Keyboard.isKeyPressed('=') || M5Cardputer.Keyboard.isKeyPressed('+')) { volume = min(21, volume + 3); volUp = true; }
      
      // VOLUME DOWN ('_' mapped to -)
      if (M5Cardputer.Keyboard.isKeyPressed('_') || M5Cardputer.Keyboard.isKeyPressed('-')) { volume = max(0, volume - 3); volUp = true; }

      // NEXT TRACK (']')
      if (M5Cardputer.Keyboard.isKeyPressed(']')) { 
        int newIndex; String next = FM.getNextAudio(cursorIdx, 1, newIndex);
        if (next != "") { cursorIdx = newIndex; playingCursorIdx = newIndex; activeFilePath = next; nextS = true; }
      }

      // PREV TRACK ('[')
      if (M5Cardputer.Keyboard.isKeyPressed('[')) { 
        int newIndex; String prev = FM.getNextAudio(cursorIdx, -1, newIndex); 
        if (prev != "") { cursorIdx = newIndex; playingCursorIdx = newIndex; activeFilePath = prev; nextS = true; }
      }
    }
  }
  
  if (showHelp) {
    if (helpRedrawNeeded) { drawHelpScreen(); drawInternalHelp(); helpRedrawNeeded = false; }
  } else {
    if (needInternalRedraw) {
        drawInternalBrowser();
        needInternalRedraw = false;
    }
    if (!isLoading) {
       drawWinampExternal();
    }
  }
  
  // FIX: Increase Main Loop Delay to spare CPU for Audio Task
  vTaskDelay(20);
}