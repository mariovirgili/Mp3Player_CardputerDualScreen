#ifndef FILEMANAGER_H
#define FILEMANAGER_H

#include <Arduino.h>
#include <SD.h>
#include <FS.h>
#include "Config.h"

// Define System Directory
#define SYSTEM_DIR "/player"
#define POS_FILE   "/player/last_pos.txt"

struct FileItem {
    String name;
    bool isDir;
};

class FileManager {
private:
    FileItem items[MAX_FILES];
    int count = 0;
    String currentPath = "/";

    // Check if file extension is supported
    bool isAudioFile(const String& name) {
        String s = name; 
        s.toLowerCase();
        // Now ALWAYS accepts both mp3 and flac, regardless of the external screen size
        return s.endsWith(".mp3") || s.endsWith(".flac");
    }

    // Save current path to SD card in /player/last_pos.txt
    void saveLastPath() {
        // Create system directory if it doesn't exist
        if (!SD.exists(SYSTEM_DIR)) {
            SD.mkdir(SYSTEM_DIR);
        }

        File f = SD.open(POS_FILE, FILE_WRITE);
        if (f) {
            f.print(currentPath);
            f.close();
        }
    }

    // Load last path from /player/last_pos.txt
    String loadLastPath() {
        if (SD.exists(POS_FILE)) {
            File f = SD.open(POS_FILE, FILE_READ);
            if (f) {
                String s = f.readString();
                s.trim();
                f.close();
                // Basic validation: make sure it starts with /
                if (s.startsWith("/")) return s;
            }
        }
        return "/";
    }

public:
    // Init SD and scan root or last saved directory
    void begin() {
        if (!SD.begin(SD_CS_GPIO)) {
            Serial.println("SD Init Failed");
        } else {
            // Ensure system directory exists
            if (!SD.exists(SYSTEM_DIR)) {
                SD.mkdir(SYSTEM_DIR);
            }

            // Try to load last position
            String lastDir = loadLastPath();
            // Check if that directory actually exists now
            if (!SD.exists(lastDir)) {
                lastDir = "/";
            }
            scanDirectory(lastDir);
        }
    }

    // Scan directory content
    void scanDirectory(String path) {
        File root = SD.open(path);
        if (!root || !root.isDirectory()) return;

        count = 0;
        currentPath = path;

        // Add parent directory option if not in root
        if (currentPath != "/" && count < MAX_FILES) {
            items[count].name = "..";
            items[count].isDir = true;
            count++;
        }

        File file = root.openNextFile();
        while (file && count < MAX_FILES) {
            String fn = String(file.name());
            
            // Remove leading slash if present
            int lastSlash = fn.lastIndexOf('/');
            if (lastSlash != -1) fn = fn.substring(lastSlash + 1);

            // SYSTEM FOLDER PROTECTION:
            // Skip the "player" folder and common system folders
            if (fn.equalsIgnoreCase("player") || fn.equalsIgnoreCase("System Volume Information")) {
                file = root.openNextFile();
                continue;
            }

            if (file.isDirectory()) {
                items[count].name = fn;
                items[count].isDir = true;
                count++;
            } else if (isAudioFile(fn)) {
                items[count].name = fn;
                items[count].isDir = false;
                count++;
            }
            file = root.openNextFile();
        }
        root.close();
    }

    // Handle selection: Enter dir or select file
    bool handleSelection(int index, String &outFilePath) {
        if (index < 0 || index >= count) return false;

        FileItem sel = items[index];

        if (sel.isDir) {
            if (sel.name == "..") {
                // Go Up
                int lastSlash = currentPath.lastIndexOf('/');
                currentPath = (lastSlash <= 0) ? "/" : currentPath.substring(0, lastSlash);
            } else {
                // Go Down
                currentPath = (currentPath == "/") ? ("/" + sel.name) : (currentPath + "/" + sel.name);
            }
            scanDirectory(currentPath);
            saveLastPath(); // Save new location persistence
            return false;
        } else {
            // File Selected
            outFilePath = (currentPath == "/") ? ("/" + sel.name) : (currentPath + "/" + sel.name);
            return true;
        }
    }

    // Find next audio file in list
    String getNextAudio(int currentIndex, int direction, int &newIndex) {
        if (count <= 0) return "";
        int i = currentIndex;
        for (int step = 0; step < count; step++) {
            i = (i + direction + count) % count;
            if (!items[i].isDir && isAudioFile(items[i].name)) {
                newIndex = i;
                return (currentPath == "/") ? ("/" + items[i].name) : (currentPath + "/" + items[i].name);
            }
        }
        return "";
    }

    // Getters
    int getCount() const { return count; }
    String getCurrentPath() const { return currentPath; }
    FileItem getItem(int index) const { if (index >= 0 && index < count) return items[index]; return {"", false}; }
};

extern FileManager FM;

#endif