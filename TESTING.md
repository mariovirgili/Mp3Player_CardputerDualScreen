# 🧪 Testing Guide

## ✅ Pre-Test Checklist

- [ ] Arduino IDE installed
- [ ] M5Stack Cardputer-Adv connected via USB
- [ ] Libraries installed:
  - [ ] ESP8266Audio
- [ ] M5Cardputer
- [ ] SD card formatted as FAT32
- [ ] MP3 files copied to SD card root
- [ ] SD card inserted into Cardputer-Adv

## 🔍 Step-by-Step Testing

### Step 1: Compile Check

1. Open `mp3_player_winamp.ino` in Arduino IDE
2. Select board: **M5Stack Cardputer-Adv**
3. Click **Verify** (✓)
4. **Expected result:** Compilation without errors

**If errors:**
- Check all libraries are installed
- Check Arduino IDE version (recommended 1.8.19+)
- Check M5Stack board support version

### Step 2: Upload

1. Select correct COM port
2. Click **Upload** (➡️)
3. Wait for upload to complete
4. **Expected result:** "Done uploading"

**If upload errors:**
- Check USB connection
- Try different USB cable
- Press RESET button on Cardputer before upload

### Step 3: Initial Boot

1. After upload, device will reboot automatically
2. **Expected result:**
   - Screen lights up
   - Winamp interface appears
   - File list displays on the left

**If screen is black:**
- Check brightness (press **L** key)
- Check display connection
- Check Serial Monitor (115200 baud) for errors

### Step 4: SD Card Detection

1. Check Serial Monitor
2. **Expected result:**
   ```
   M5Mp3 Winamp Player for Cardputer-Adv
   Listing directory: /
   FILE: song1.mp3
   FILE: song2.mp3
   Found X MP3 files
   Setup complete!
   ```

**If "SD Mount Failed":**
- Check SD card format (must be FAT32)
- Try different SD card
- Check card insertion

**If "No MP3 files":**
- Ensure files are in root directory
- Check file extensions (.mp3, not .MP3)
- Maximum 100 files

### Step 5: Audio Playback

1. Press **A** key for Play/Pause
2. **Expected result:**
   - Track starts playing
   - "PLAY" displays on screen
   - Visualization (graph) animates
   - Sound comes from speaker or headphones

**If no sound:**
- Press **V** several times to increase volume
- Check if headphones are connected (if using)
- Check MP3 file format (128-192 kbps recommended)
- Check Serial Monitor for decoding errors

**If crackling/interruptions:**
- Use MP3 files with lower bitrate
- Check SD card speed (Class 10 recommended)
- Ensure SD card is not damaged

### Step 6: Controls Test

Test each key:

| Key | Expected Result |
|-----|----------------|
| **A** | Play/Pause toggles |
| **N** | Next track |
| **P** | Previous track |
| **V** | Volume increases (5→10→15→20→5) |
| **L** | Brightness changes (5 levels) |
| **B** | Random track |
| **ENTER** | Track restarts |
| **;** | Scroll list up |
| **.** | Scroll list down |

### Step 7: Visual Elements

Check display:

- [ ] File list (left side)
- [ ] Current track highlight (white color)
- [ ] Scroll slider (right side)
- [ ] Track name (scrolling)
- [ ] Time (MM:SS format)
- [ ] Battery indicator (%)
- [ ] Equalizer graph (animated)
- [ ] Control buttons (A, P, N, B)
- [ ] Volume bar (yellow)
- [ ] Brightness bar (magenta)

## 🐛 Common Issues & Solutions

### Issue: Compilation Error - "AudioOutput.h: No such file"

**Solution:**
```bash
# Install ESP8266Audio library
# Arduino IDE → Sketch → Include Library → Manage Libraries
# Search: "ESP8266Audio"
# Install by earlephilhower
```

### Issue: "SD Mount Failed"

**Solution:**
1. Format SD card as FAT32
2. Use Class 10 or higher card
3. Check card insertion
4. Try different SD card

### Issue: "No MP3 files found"

**Solution:**
1. Ensure files are in root directory (not in subfolders)
2. Check file extensions (.mp3)
3. Maximum 100 files supported
4. File names without special characters

### Issue: Playback Stuttering

**Solution:**
1. Use MP3 files with bitrate 128-192 kbps
2. Use Class 10 SD card
3. Ensure files are not corrupted
4. Try different MP3 file

### Issue: No Sound

**Solution:**
1. Press **V** to increase volume
2. Check headphone connection (if using)
3. Check that speaker is not blocked
4. Check Serial Monitor for errors

### Issue: Screen Flickering

**Solution:**
1. This is normal - screen updates every 40ms
2. Can reduce update frequency in code (Task_TFT delay)

## 📊 Performance Metrics

After successful testing, check:

- **Startup Time:** < 3 seconds
- **File Scan Time:** < 1 second per 10 files
- **Track Switch Time:** < 1 second
- **Audio Latency:** < 100ms
- **CPU Usage:** < 80% (can check via Serial Monitor)

## ✅ Success Criteria

Project is considered successfully tested if:

- [x] Code compiles without errors
- [x] Successfully uploads to device
- [x] SD card detected
- [x] MP3 files found and displayed
- [x] Playback works
- [x] Sound is clean without crackling
- [x] All control keys work
- [x] Winamp interface displays correctly
- [x] Track switching works
- [x] Volume and brightness adjust

## 📝 Test Results Template

```
Date: ___________
Tester: ___________
Device: M5Stack Cardputer-Adv
SD Card: ___________
MP3 Files: _____ files

[ ] Compilation: PASS / FAIL
[ ] Upload: PASS / FAIL
[ ] SD Detection: PASS / FAIL
[ ] File Scan: PASS / FAIL (found: ___ files)
[ ] Playback: PASS / FAIL
[ ] Sound Quality: EXCELLENT / GOOD / POOR
[ ] Controls: PASS / FAIL
[ ] Interface: PASS / FAIL

Notes:
_______________________________________
_______________________________________
```

---

**Good luck with testing! 🎵**
