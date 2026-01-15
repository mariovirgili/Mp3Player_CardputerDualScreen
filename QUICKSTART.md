# 🚀 Quick Start Guide

## 📋 Prerequisites

1. **Arduino IDE** or **PlatformIO**
2. **M5Stack Cardputer-Adv**
3. **microSD card** (FAT32 format)
4. **MP3 files** for testing

## 📦 Installation Steps

### Step 1: Install Libraries

Install via Arduino IDE Library Manager:

1. **ESP8266Audio**
   - Sketch → Include Library → Manage Libraries
   - Search: "ESP8266Audio"
   - Install by earlephilhower

2. ~~**ESP32Time**~~ - Not required (uses built-in timer)

3. **M5Cardputer** (should already be installed)

### Step 2: Prepare SD Card

1. Format SD card as **FAT32**
2. Copy MP3 files to root directory:
   ```
   /song1.mp3
   /song2.mp3
   /song3.mp3
   ```
3. Insert SD card into Cardputer-Adv

### Step 3: Upload Code

1. Open `mp3_player_winamp.ino` in Arduino IDE
2. Select board: **M5Stack Cardputer-Adv**
3. Select port (COM port of your device)
4. Click **Upload** ⬆️

### Step 4: Enjoy! 🎵

After upload, the player will start automatically!

## 🎮 Basic Controls

- **A** - Play/Pause
- **N** - Next track
- **P** - Previous track  
- **V** - Volume up (cycles: 5→10→15→20→5)
- **L** - Brightness control
- **B** - Random track

## ⚠️ Troubleshooting

### No sound?
- Check SD card is inserted correctly
- Increase volume (press **V** key)
- Check file format (MP3 only)

### SD card not detected?
- Format as FAT32
- Check card insertion
- Try different SD card

### Files not showing?
- Only `.mp3` files in root directory
- Maximum 100 files
- Check file names (no spaces recommended)

## 📝 Notes

- Winamp interface fully preserved
- Uses ES8311 codec via M5Cardputer.Speaker
- FreeRTOS tasks for smooth operation
- Triple buffering for smooth playback

## 🎯 Next Steps

After successful launch you can:
- Add more MP3 files
- Adjust screen brightness (press **L** key)
- Experiment with controls

---

**Good luck! 🎵**
