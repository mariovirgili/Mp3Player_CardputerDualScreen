# 📝 Changes from Original M5Mp3

## 🔄 Key Adaptations

### 1. Audio Library Replacement
**Original:** ESP32-audioI2S (I2S direct output)  
**New:** ESP8266Audio (MP3 decoder) + Custom AudioOutput

**Why:**
- ESP32-audioI2S works with direct I2S (pins 42, 41, 43)
- Cardputer-Adv uses ES8311 via M5Cardputer.Speaker API
- ESP8266Audio is more flexible and supports custom AudioOutput

### 2. Custom AudioOutput Class
**Created:** `AudioOutputM5CardputerSpeaker`

**Features:**
- Inherits from `AudioOutput` (ESP8266Audio)
- Uses `M5Cardputer.Speaker.playRaw()` for PCM output
- Triple buffering for smooth playback
- Stereo → mono conversion (ES8311 is mono)
- Automatic wait for playback completion

**Key Methods:**
```cpp
ConsumeSample(int16_t sample[2])  // Receives stereo samples from decoder
flush()                           // Sends buffer to M5.Speaker
stop()                            // Stops playback
```

### 3. Removed I2S Configuration
**Removed:**
```cpp
#define I2S_DOUT 42
#define I2S_BCLK 41
#define I2S_LRCK 43
audio.setPinout(I2S_BCLK, I2S_LRCK, I2S_DOUT);
```

**Replaced with:**
```cpp
M5Cardputer.Speaker.begin();
M5Cardputer.Speaker.setVolume(volume);
```

### 4. Audio Playback Logic
**Original:**
```cpp
audio.connecttoFS(SD, filename);
audio.loop();  // In task
```

**New:**
```cpp
AudioFileSourceSD file(filename);
AudioFileSourceID3 id3(&file);
AudioGeneratorMP3 mp3;
mp3.begin(&id3, audioOut);
mp3.loop();  // In task
```

### 5. Volume Control
**Original:**
```cpp
audio.setVolume(volume);  // 0-21
```

**New:**
```cpp
// Dual control:
audioOut->SetGain((float)volume / 21.0f);  // ESP8266Audio gain
M5Cardputer.Speaker.setVolume(map(volume, 0, 21, 0, 255));  // M5.Speaker volume
```

### 6. Battery Reading
**Original:**
```cpp
analogRead(10)  // Direct ADC reading
```

**New:**
```cpp
M5Cardputer.Power.getBatteryVoltage()  // Proper API
```

### 7. File Filtering
**Added:** Only `.mp3` files are scanned (case-insensitive)

**Original:** All files  
**New:** Only `.mp3` files

## ✅ Preserved Features

- ✅ Winamp-style interface (100% preserved)
- ✅ FreeRTOS tasks (TFT + Audio)
- ✅ File browser with scroll
- ✅ Volume control (5 levels)
- ✅ Brightness control
- ✅ Battery indicator
- ✅ Visual equalizer (animated bars)
- ✅ Scrolling track name
- ✅ All keyboard controls

## 🔧 Technical Improvements

1. **Better Error Handling:**
   - SD card mount check
   - File count validation
   - Empty file list handling

2. **Memory Management:**
   - Proper cleanup of audio objects
   - Dynamic allocation/deallocation

3. **Smoother Playback:**
   - Triple buffering prevents audio glitches
   - Proper wait for playback completion

4. **Code Organization:**
   - Clear separation of concerns
   - Well-commented code
   - Consistent naming

## 📊 Performance

- **Audio Quality:** Same as original (MP3 decoding)
- **CPU Usage:** Similar (FreeRTOS tasks)
- **Memory:** Slightly higher (ESP8266Audio overhead)
- **Latency:** Minimal (triple buffering)

## 🐛 Known Limitations

1. **Mono Output Only:**
   - ES8311 on Cardputer-Adv is mono
   - Stereo converted to mono (average L+R)

2. **No Hardware Stop:**
   - `M5Cardputer.Speaker` doesn't have `stop()` method
   - Uses wait for playback completion instead

3. **File Limit:**
   - Maximum 100 files (same as original)
   - Root directory only

## 🎯 Future Enhancements

- [ ] WAV file support
- [ ] Subdirectory support
- [ ] ID3 tag display
- [ ] Playlist support
- [ ] External display (ILI9488) support
- [ ] Shuffle mode
- [ ] Repeat mode
