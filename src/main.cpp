#include <Arduino.h>
#include "Config.h"

// ===================== 1. FONT TRICK =====================
// Needed to use custom fonts with TFT_eSPI without conflicts
#include <TFT_eSPI.h> 
typedef GFXfont TFT_Native_Font; 

namespace MyFonts {
  #include <Fonts/GFXFF/FreeSansBold9pt7b.h>
  #include <Fonts/GFXFF/FreeSansBold12pt7b.h>
}

// ===================== 2. LIBRARIES =====================
// Workaround for some SPI definition conflicts on ESP32
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
#include "Buttons.h" // Includes Buttons AND Codec Images

// Ensure KEY_ESC is defined (Standard ASCII 27)
#ifndef KEY_ESC
#define KEY_ESC 27
#endif

FileManager FM;

// ===================== DISPLAY OBJECTS =====================
TFT_eSPI externalDisplay = TFT_eSPI();
TFT_eSprite sprite = TFT_eSprite(&externalDisplay);
TFT_eSprite buttonsLayer = TFT_eSprite(&externalDisplay);

// ===================== AUDIO CLASS =====================
// Custom AudioOutput class for M5Cardputer Speaker (PCM5102)
class AudioOutputM5CardputerSpeaker : public AudioOutput {
public:
  AudioOutputM5CardputerSpeaker(m5::Speaker_Class* m5sound) { _m5sound = m5sound; }
  virtual bool begin() override { return true; }
  virtual bool ConsumeSample(int16_t sample[2]) override {
    // Record peaks for VU Meter
    int16_t l = abs(sample[0]);
    int16_t r = abs(sample[1]);
    if (l > peakL) peakL = l;
    if (r > peakR) peakR = r;

    // Buffer filling for stereo to mono mix/output
    if (_tri_buffer_index < 1536) {
      _tri_buffer[_tri_index][_tri_buffer_index++] = (int16_t)(((int32_t)sample[0] + sample[1]) / 2);
      return true;
    }
    flush(); return false;
  }
  virtual void flush() override {
    if (_tri_buffer_index > 0) {
      while (_m5sound->isPlaying()) vTaskDelay(1);
      _m5sound->playRaw(_tri_buffer[_tri_index], _tri_buffer_index, 44100, false);
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

// ===================== GLOBAL VARIABLES =====================
unsigned short grays[18];
int cursorIdx = 0;
int playingCursorIdx = 0; 
int volume = 12; // Initial Volume

// --- STATE MACHINE ---
enum PlayerState {
  STATE_STOPPED,
  STATE_PLAYING,
  STATE_PAUSED
};
PlayerState currentState = STATE_STOPPED;

bool nextS = false;
bool volUp = false;

// Help Screen State
bool showHelp = false; 
bool helpRedrawNeeded = false; 

// --- SCREENSAVER LOGIC (NEW) ---
unsigned long lastInputTime = 0;
bool isScreenOn = true;
const unsigned long SCREEN_TIMEOUT = 10000; // 10 Seconds
const uint8_t SCREEN_BRIGHTNESS_ON = 100;   // Default Brightness

// Timer Logic
unsigned long trackStartTime = 0;
unsigned long pausedAt = 0; 
uint32_t trackDurationSec = 0;

String activeFilePath = "", currentCodec = "NONE";
int sampleRate = 0, bitRate = 0;

// UI State variables
static int vuL = 0, vuR = 0;
static int marqueeX = 300, marqueeW = 0;
static unsigned long lastMarqueeMs = 0, lastBattMs = 0;
static int battPct = 0;
static float battVolts = 0.0f; 

// Audio Objects
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
  // 1. Internal Screen
  M5Cardputer.Display.setBrightness(SCREEN_BRIGHTNESS_ON); // Ensure screen is ON
  M5Cardputer.Display.fillScreen(TFT_BLACK);
  int ix = (M5Cardputer.Display.width() - internalLogoW) / 2;
  int iy = (M5Cardputer.Display.height() - internalLogoH) / 2;
  M5Cardputer.Display.setSwapBytes(true);
  M5Cardputer.Display.pushImage(ix, iy, internalLogoW, internalLogoH, logoInternal);
  
  // 2. External Screen
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
  // Do not draw if screen is effectively off (optimization)
  if (!isScreenOn) return;

  M5Cardputer.Display.fillScreen(TFT_BLACK);
  M5Cardputer.Display.setTextSize(1);
  M5Cardputer.Display.setTextDatum(TC_DATUM); 
  
  M5Cardputer.Display.setTextColor(TFT_ORANGE);
  M5Cardputer.Display.drawString("Mp3 Player v0.1 - Browser", 120, 5); 

  M5Cardputer.Display.setTextColor(TFT_LIGHTGREY);
  M5Cardputer.Display.drawString("H: Help | A: Play | S: Stop", 120, 15);

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
      if (item.isDir) {
        itemColor = TFT_CYAN;
      } else {
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

// === INTERNAL HELP SCREEN ===
void drawInternalHelp() {
  if (!isScreenOn) return;

  M5Cardputer.Display.fillScreen(TFT_BLACK);
  
  // Double Border
  M5Cardputer.Display.drawRect(0, 0, 240, 135, TFT_BLUE);
  M5Cardputer.Display.drawRect(3, 3, 234, 129, TFT_WHITE);

  M5Cardputer.Display.setTextSize(1);
  M5Cardputer.Display.setTextDatum(TC_DATUM);
  M5Cardputer.Display.setTextColor(TFT_YELLOW);
  M5Cardputer.Display.drawString("KEYBOARD CONTROLS", 120, 10);
  
  M5Cardputer.Display.drawFastHLine(10, 22, 220, TFT_BLUE);
  
  M5Cardputer.Display.setTextDatum(TL_DATUM);
  M5Cardputer.Display.setTextColor(TFT_WHITE);
  
  int y = 30;
  int x = 20; 
  int spacing = 14; 
  
  M5Cardputer.Display.drawString("A       : Play / Pause", x, y); y += spacing;
  M5Cardputer.Display.drawString("S       : Stop", x, y); y += spacing;
  M5Cardputer.Display.drawString("N / P   : Next / Prev Track", x, y); y += spacing;
  M5Cardputer.Display.drawString("V       : Volume Up", x, y); y += spacing;
  M5Cardputer.Display.drawString("; / .   : Nav Up / Down", x, y); y += spacing;
  M5Cardputer.Display.drawString("ENTER   : Select / Open", x, y); y += spacing;
  
  M5Cardputer.Display.setTextDatum(TC_DATUM);
  M5Cardputer.Display.setTextColor(TFT_RED);
  M5Cardputer.Display.drawString("H / ESC : CLOSE HELP", 120, 118);
}

// === EXTERNAL HELP SCREEN ===
void drawHelpScreen() {
  sprite.fillSprite(TFT_BLACK);

  // Double Border
  sprite.drawRect(10, 10, 300, 220, TFT_BLUE);
  sprite.drawRect(14, 14, 292, 212, TFT_WHITE);

  sprite.setFreeFont((const TFT_Native_Font*)&MyFonts::FreeSansBold12pt7b);
  sprite.setTextColor(TFT_YELLOW, TFT_BLACK);
  sprite.setTextDatum(TC_DATUM);
  sprite.drawString("COMMANDS LIST", 160, 25);

  sprite.setFreeFont((const TFT_Native_Font*)&MyFonts::FreeSansBold9pt7b);
  sprite.setTextDatum(TL_DATUM);
  
  int yStart = 55;
  int lineH = 25;
  int colKey = 40;
  int colDesc = 120;

  auto drawLine = [&](String key, String desc, int row) {
    int y = yStart + (row * lineH);
    sprite.setTextColor(TFT_CYAN, TFT_BLACK); 
    sprite.drawString(key, colKey, y);
    sprite.setTextColor(TFT_WHITE, TFT_BLACK); 
    sprite.drawString(desc, colDesc, y);
  };

  drawLine("A",       "Play / Pause", 0);
  drawLine("S",       "Stop Playback", 1);
  drawLine("N / P",   "Next / Prev Track", 2);
  drawLine("V",       "Volume +", 3);
  drawLine("; / .",   "Nav Up/Down", 4);
  drawLine("ENTER",   "Select / Open", 5);
  
  sprite.setTextDatum(TC_DATUM);
  sprite.setTextColor(TFT_RED, TFT_BLACK);
  sprite.setFreeFont(NULL); 
  sprite.setTextFont(2);    
  sprite.setTextSize(1);
  sprite.drawString("PRESS [H] or [ESC] TO CLOSE", 160, 205); 

  sprite.pushSprite(0, 0);
}

// === BUTTONS LAYER (54x35 BITMAPS) ===
static void buildButtonsLayer(uint16_t bgPanel) {
  if (!buttonsLayer.created()) buttonsLayer.createSprite(320, 42);
  buttonsLayer.fillSprite(bgPanel);
  
  // Logic: 0=PREV, 1=PLAY, 2=PAUSE, 3=STOP, 4=NEXT
  for (int i = 0; i < 5; i++) {
    int x = 10 + (i * 60); 

    const unsigned short* imgPtr = nullptr;

    switch(i) {
      case 0: // PREV
        imgPtr = btnPrev;
        break;
      case 1: // PLAY
        if (currentState == STATE_PLAYING) imgPtr = btnPlayOn;
        else imgPtr = btnPlayOff;
        break;
      case 2: // PAUSE
        if (currentState == STATE_PAUSED) imgPtr = btnPauseOn;
        else imgPtr = btnPauseOff;
        break;
      case 3: // STOP
        if (currentState == STATE_STOPPED) imgPtr = btnStopOn;
        else imgPtr = btnStopOff;
        break;
      case 4: // NEXT
        imgPtr = btnNext;
        break;
    }

    if (imgPtr != nullptr) {
      buttonsLayer.setSwapBytes(true);
      buttonsLayer.pushImage(x, 3, btnW, btnH, imgPtr);
      buttonsLayer.setSwapBytes(false);
    }
  }
}

void drawWinampExternal() {
  if (millis() - lastBattMs > 2000) {
    lastBattMs = millis();
    battPct = M5Cardputer.Power.getBatteryLevel();
    int voltageMv = M5Cardputer.Power.getBatteryVoltage();
    battVolts = voltageMv / 1000.0; 
  }

  const uint16_t titleBar = externalDisplay.color565(50, 50, 50); 
  const uint16_t bgPanel  = externalDisplay.color565(57, 52, 33);
  sprite.fillSprite(bgPanel);

  sprite.fillRect(2, 2, 316, 32, titleBar);

  // Draw Title Bar Logo (Left)
  sprite.setSwapBytes(true); 
  sprite.pushImage(6, 1, titleLogoW, titleLogoH, logoTitleBar);
  sprite.setSwapBytes(false); 

  // === NEW CODEC LOGO LOGIC (Right) ===
  const unsigned short* codecImgPtr = nullptr;
  int codecW = 0;
  int codecH = 32; // Default height

  // MODIFIED LOGIC: Check State FIRST
  if (currentState == STATE_STOPPED) {
    // If Stopped -> Show Startup/None Logo
    codecImgPtr = imgCodecNone;
    codecW = codecNoneW; // 75
  }
  else if (currentCodec == "MP3") {
    codecImgPtr = imgCodecMp3;
    codecW = codecAudioW; // 59
  }
  else if (currentCodec == "FLAC") {
    codecImgPtr = imgCodecFlac;
    codecW = codecAudioW; // 59
  } 
  else {
    // Fallback
    codecImgPtr = imgCodecNone;
    codecW = codecNoneW; // 75
  }

  // Draw right-aligned (Screen 320, TitleBar end ~318)
  if (codecImgPtr != nullptr) {
    int xPos = 320 - codecW - 4; // 4px margin from right edge
    int yPos = 2; // Top of title bar

    sprite.setSwapBytes(true);
    sprite.pushImage(xPos, yPos, codecW, codecH, codecImgPtr);
    sprite.setSwapBytes(false);
  }
  // ===================================

  sprite.setTextDatum(TL_DATUM); 
  sprite.fillRect(10, 38, 300, 92, TFT_BLACK);
  
  // === VU METERS ===
  if (currentState == STATE_PLAYING && audioOut) {
    vuL = constrain(map(audioOut->peakL, 0, 32000, 0, 75), 0, 75); audioOut->peakL = 0;
    vuR = constrain(map(audioOut->peakR, 0, 32000, 0, 75), 0, 75); audioOut->peakR = 0;
  } else { vuL = 0; vuR = 0; }
  
  auto drawVU = [&](int x, int val) {
    sprite.drawRect(x, 45, 20, 75, grays[6]); 
    for(int i=0; i<val; i+=4) {
      uint16_t c = (i < 45) ? TFT_GREEN : (i < 65 ? TFT_YELLOW : TFT_RED);
      if(118-i >= 45) sprite.fillRect(x+2, 118-i, 16, 3, c); 
    }
  };
  drawVU(20, vuL); 
  drawVU(45, vuR); 

  // === TRACK INFO ===
  sprite.setFreeFont(NULL);
  sprite.setTextFont(2); 
  sprite.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  sprite.setTextDatum(TL_DATUM); 
  
  sprite.drawString("Bitrate: " + String(bitRate/1000) + " kbps", 75, 50);
  sprite.drawString("Freq: " + String((float)sampleRate/1000.0, 1) + " kHz", 75, 70);

  // === TIMER LOGIC ===
  unsigned long elapsed = 0;
  if (currentState == STATE_STOPPED) {
    elapsed = 0;
  } else if (currentState == STATE_PLAYING) {
    elapsed = (millis() - trackStartTime) / 1000;
  } else if (currentState == STATE_PAUSED) {
    elapsed = (pausedAt - trackStartTime) / 1000;
  }
  
  sprite.setTextFont(4); 
  sprite.setTextColor(TFT_GREEN, TFT_BLACK); 
  sprite.setTextDatum(TR_DATUM); 
  sprite.drawString(timeMMSS(elapsed), 290, 48); 

  sprite.setTextFont(2); 
  sprite.setTextColor(TFT_WHITE, TFT_BLACK);
  if (trackDurationSec > 0) {
    sprite.drawString("/ " + timeMMSS(trackDurationSec), 290, 75); 
  } else {
    sprite.drawString("/ --:--", 290, 75);
  }

  sprite.setTextDatum(TL_DATUM);
  sprite.setFreeFont(NULL);

  // === NEXT SONG ===
  sprite.drawFastHLine(85, 95, 220, grays[6]); 
  
  sprite.setTextColor(TFT_ORANGE, TFT_BLACK);
  sprite.setTextFont(1); 
  sprite.drawString("NEXT TRACK:", 85, 100);
  
  int nextIdxDummy;
  String nextTrackPath = FM.getNextAudio(playingCursorIdx, 1, nextIdxDummy);
  String nextTrackName = (nextTrackPath != "") ? baseName(nextTrackPath) : "---";
  
  sprite.setFreeFont((const TFT_Native_Font*)&MyFonts::FreeSansBold9pt7b);
  sprite.setTextColor(TFT_WHITE, TFT_BLACK);
  if (nextTrackName.length() > 22) nextTrackName = nextTrackName.substring(0, 20) + "..";
  sprite.drawString(nextTrackName, 85, 112);

  // === PROGRESS BAR ===
  sprite.drawRect(10, 134, 300, 6, grays[6]);
  if (currentState != STATE_STOPPED && trackDurationSec > 0) {
    float p = (float)elapsed / trackDurationSec;
    sprite.fillRect(11, 135, (int)(min(1.0f, p) * 298), 4, TFT_GOLD);
  }

  // === MARQUEE ===
  sprite.fillRect(10, 144, 300, 38, TFT_BLACK);
  sprite.drawRect(10, 144, 300, 38, grays[8]);
  String title = (activeFilePath != "") ? baseName(activeFilePath) : "READY";
  
  sprite.setViewport(11, 145, 298, 36);
  sprite.setFreeFont((const TFT_Native_Font*)&MyFonts::FreeSansBold12pt7b); 
  marqueeW = sprite.textWidth(title);
  if (millis() - lastMarqueeMs >= 35) { 
    lastMarqueeMs = millis(); 
    marqueeX -= 3; 
    if (marqueeX < -marqueeW) marqueeX = 300; 
  }
  
  // FIXED: Re-added codecColor definition here for the Marquee text color
  uint16_t codecColor = (currentCodec == "FLAC") ? TFT_CYAN : TFT_GOLD;
  
  sprite.setTextColor(codecColor, TFT_BLACK); 
  sprite.drawString(title, marqueeX, 6);
  sprite.resetViewport();

  // === BOTTOM BAR ===
  sprite.setFreeFont(NULL);
  sprite.setTextSize(1);
  sprite.drawRect(10, 186, 16, 8, TFT_LIGHTGREY);
  
  uint16_t battColor = (battPct > 20) ? TFT_GREEN : TFT_RED;
  sprite.fillRect(11, 187, (14 * battPct)/100, 6, battColor);
  
  sprite.setTextColor(TFT_WHITE); 
  sprite.setCursor(32, 187); 
  sprite.printf("%d%% %.2fV", battPct, battVolts);

  sprite.drawRect(110, 186, 200, 8, grays[4]);
  
  int volWidth = map(volume, 0, 21, 0, 198); 
  volWidth = constrain(volWidth, 0, 198);    
  sprite.fillRect(111, 187, volWidth, 6, TFT_CYAN);

  buildButtonsLayer(bgPanel); 
  buttonsLayer.pushToSprite(&sprite, 0, 198);
  sprite.pushSprite(0, 0);
}

// ===================== AUDIO LOGIC TASK =====================
void Task_Audio(void *p) {
  while (1) {
    if (volUp) { M5Cardputer.Speaker.setVolume(map(volume, 0, 21, 0, 255)); volUp = false; }
    
    if (nextS) {
      if (audioGen) { audioGen->stop(); delete audioGen; audioGen = nullptr; }
      if (audioId3) { delete audioId3; audioId3 = nullptr; }
      if (audioFile) { delete audioFile; audioFile = nullptr; }

      audioFile = new AudioFileSourceSD(activeFilePath.c_str());
      uint32_t fsize = audioFile->getSize();
      sampleRate = 44100;

      if (activeFilePath.endsWith(".flac") || activeFilePath.endsWith(".FLAC")) { 
        audioGen = new AudioGeneratorFLAC(); 
        currentCodec = "FLAC";
        bitRate = 700000; 
        trackDurationSec = (fsize * 8) / 700000; 
      } else {
        audioGen = new AudioGeneratorMP3(); 
        currentCodec = "MP3";
        bitRate = 128000; 
        trackDurationSec = (fsize * 8) / 128000; 
      }
      
      audioId3 = new AudioFileSourceID3(audioFile);
      audioGen->begin(audioId3, audioOut);
      
      trackStartTime = millis(); 
      currentState = STATE_PLAYING; 
      nextS = false; 
      marqueeX = 300;
    }
    
    if (currentState == STATE_PLAYING && audioGen && audioGen->isRunning()) {
      if (!audioGen->loop()) { 
        currentState = STATE_STOPPED; 
      }
    }
    vTaskDelay(1);
  }
}

// ===================== SETUP =====================
void setup() {
  auto cfg = M5.config(); M5Cardputer.begin(cfg);
  pinMode(3, OUTPUT); digitalWrite(3, LOW); delay(100); digitalWrite(3, HIGH);
  
  // 1. INIT DISPLAY
  externalDisplay.begin(); externalDisplay.setRotation(3);
  int co = 220; for (int i = 0; i < 18; i++) { grays[i] = externalDisplay.color565(co, co, co); co -= 11; }
  sprite.createSprite(320, 240);
  
  // 2. DRAW SPLASH IMAGE
  showSplashScreen();
  
  // Initialize timer
  lastInputTime = millis();

  // 3. INIT SD
  SPI.begin(SD_SCK_GPIO, SD_MISO_GPIO, SD_MOSI_GPIO, SD_CS_GPIO);
  FM.begin(); 
  
  // 4. INIT AUDIO & PLAY JINGLE
  M5Cardputer.Speaker.begin();
  M5Cardputer.Speaker.setVolume(map(volume, 0, 21, 0, 255)); 
  
  if (SD.exists("/player/intro.mp3")) {
      AudioFileSourceSD *jingleFile = new AudioFileSourceSD("/player/intro.mp3");
      AudioGeneratorMP3 *jingleGen = new AudioGeneratorMP3();
      AudioOutputM5CardputerSpeaker *jingleOut = new AudioOutputM5CardputerSpeaker(&M5Cardputer.Speaker);
      
      jingleGen->begin(new AudioFileSourceID3(jingleFile), jingleOut);
      while(jingleGen->isRunning()) { if (!jingleGen->loop()) jingleGen->stop(); }
      delete jingleGen; delete jingleFile; delete jingleOut;
  } else {
      delay(2000); 
  }

  audioOut = new AudioOutputM5CardputerSpeaker(&M5Cardputer.Speaker);
  
  drawInternalBrowser();
  xTaskCreatePinnedToCore(Task_Audio, "Audio", 10240, NULL, 3, NULL, 1);
}

// ===================== MAIN LOOP =====================
void loop() {
  M5Cardputer.update();
  
  // SCREENSAVER LOGIC
  if (isScreenOn && (millis() - lastInputTime > SCREEN_TIMEOUT)) {
      M5Cardputer.Display.setBrightness(0);
      isScreenOn = false;
  }

  if (M5Cardputer.Keyboard.isChange()) {
    
    // WAKE UP
    lastInputTime = millis();
    if (!isScreenOn) {
        M5Cardputer.Display.setBrightness(SCREEN_BRIGHTNESS_ON);
        isScreenOn = true;
    }

    // HELP TOGGLE
    if (M5Cardputer.Keyboard.isKeyPressed(KEY_ESC) || 
        M5Cardputer.Keyboard.isKeyPressed('`') || 
        M5Cardputer.Keyboard.isKeyPressed('h')) {
      showHelp = !showHelp;
      
      if (showHelp) {
        helpRedrawNeeded = true;
      } else {
        drawInternalBrowser();
      }
    }

    if (!showHelp) {
      if (M5Cardputer.Keyboard.isKeyPressed(';')) { 
        cursorIdx = (cursorIdx - 1 + FM.getCount()) % FM.getCount(); 
        drawInternalBrowser(); 
      }
      if (M5Cardputer.Keyboard.isKeyPressed('.')) { 
        cursorIdx = (cursorIdx + 1) % FM.getCount(); 
        drawInternalBrowser(); 
      }
      
      if (M5Cardputer.Keyboard.isKeyPressed(13) || M5Cardputer.Keyboard.isKeyPressed(10) || M5Cardputer.Keyboard.isKeyPressed(KEY_ENTER)) {
        String newFile;
        bool isFile = FM.handleSelection(cursorIdx, newFile);
        if (isFile) {
          activeFilePath = newFile;
          playingCursorIdx = cursorIdx; 
          nextS = true; 
        } else {
          cursorIdx = 0;
          drawInternalBrowser();
        }
      }

      if (M5Cardputer.Keyboard.isKeyPressed('a')) { 
        if (currentState == STATE_PLAYING) {
          currentState = STATE_PAUSED;
          pausedAt = millis();
        } 
        else if (currentState == STATE_PAUSED) {
          currentState = STATE_PLAYING;
          trackStartTime += (millis() - pausedAt);
          pausedAt = 0;
        }
        else if (currentState == STATE_STOPPED && activeFilePath != "") {
           nextS = true; 
        }
      }

      if (M5Cardputer.Keyboard.isKeyPressed('s')) { 
        if (audioGen) audioGen->stop(); 
        currentState = STATE_STOPPED;
        trackStartTime = 0; 
      }
      
      if (M5Cardputer.Keyboard.isKeyPressed('v')) { volume = (volume + 3) % 24; volUp = true; }
      
      if (M5Cardputer.Keyboard.isKeyPressed('n')) { 
        int newIndex;
        String next = FM.getNextAudio(cursorIdx, 1, newIndex);
        if (next != "") {
          cursorIdx = newIndex;
          playingCursorIdx = newIndex; 
          activeFilePath = next;
          nextS = true;
        }
      }
      
      if (M5Cardputer.Keyboard.isKeyPressed('p')) { 
        int newIndex;
        String prev = FM.getNextAudio(cursorIdx, -1, newIndex); 
        if (prev != "") {
          cursorIdx = newIndex;
          playingCursorIdx = newIndex; 
          activeFilePath = prev;
          nextS = true;
        }
      }
    }
  }
  
  if (showHelp) {
    if (helpRedrawNeeded) {
      drawHelpScreen();
      drawInternalHelp();
      helpRedrawNeeded = false; 
    }
  } else {
    drawWinampExternal();
  }
  
  vTaskDelay(35);
}