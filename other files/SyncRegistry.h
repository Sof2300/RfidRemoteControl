#ifndef SYNCREGISTRY_H
#define SYNCREGISTRY_H

#include <Arduino.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <map>

class SyncRegistry {
private:
    std::map<String, String> _rfidMap;
    uint32_t _version = 0;
    uint32_t _count = 0;
    String _baseUrl;
    const char* _ptrPath = "/sync.ptr";
    const char* _dbPath = "/rfid.jsonl";

    // Load metadata (version/count)
    void loadPtr() {
        if (!LittleFS.exists(_ptrPath)) return;
        File f = LittleFS.open(_ptrPath, "r");
        StaticJsonDocument<128> doc;
        deserializeJson(doc, f);
        _version = doc["v"] | 0;
        _count = doc["c"] | 0;
        f.close();
    }

    // Save metadata
    void savePtr() {
        File f = LittleFS.open(_ptrPath, "w");
        StaticJsonDocument<128> doc;
        doc["v"] = _version;
        doc["c"] = _count;
        serializeJson(doc, f);
        f.close();
    }

    // Append a single JSONL line to local storage
    void appendLocal(String tag, String id) {
        File f = LittleFS.open(_dbPath, "a");
        if (f) {
            f.printf("{\"t\":\"%s\",\"i\":\"%s\"}\n", tag.c_str(), id.c_str());
            f.close();
            _rfidMap[tag] = id;
            _count++;
        }
    }

public:
    bool begin(String baseUrl) {
        _baseUrl = baseUrl;
        if (!_baseUrl.endsWith("/")) _baseUrl += "/";
        
        if (!LittleFS.begin(true)) {
            Serial.println("LittleFS Mount Failed");
            return false;
        }

        loadPtr();
        loadDatabase();
        return true;
    }

    // Load the JSONL file line-by-line into RAM
    void loadDatabase() {
        if (!LittleFS.exists(_dbPath)) return;
        File f = LittleFS.open(_dbPath, "r");
        _rfidMap.clear();
        uint32_t actualCount = 0;

        while (f.available()) {
            String line = f.readStringUntil('\n');
            if (line.length() < 5) continue;
            StaticJsonDocument<256> doc;
            if (deserializeJson(doc, line) == DeserializationError::Ok) {
                _rfidMap[doc["t"].as<String>()] = doc["i"].as<String>();
                actualCount++;
            }
        }
        f.close();
        _count = actualCount;
        Serial.printf("Database loaded. Records: %d, Version: %d\n", _count, _version);
    }

    String getId(String tag) {
        if (_rfidMap.count(tag)) return _rfidMap[tag];
        return "";
    }

    // MAIN SYNC LOGIC
    void sync() {
        if (WiFi.status() != WL_CONNECTED) return;

        HTTPClient http;
        // 1. Check Server State
        http.begin(_baseUrl + "sync-check");
        int code = http.GET();
        if (code != 200) { http.end(); return; }

        StaticJsonDocument<256> serverState;
        deserializeJson(serverState, http.getString());
        http.end();

        uint32_t remoteVersion = serverState["v"];
        uint32_t remoteCount = serverState["c"];

        if (remoteVersion <= _version) {
            Serial.println("System in sync.");
            return;
        }

        // 2. Decide: Delta (Append) or Full (Rewrite)
        if (remoteCount > _count && remoteVersion > _version) {
            // Check if it's a simple append (server should verify this logic)
            fetchDelta();
        } else {
            fetchFull();
        }
    }

    void fetchDelta() {
        Serial.println("Sync: Fetching Delta...");
        HTTPClient http;
        http.begin(_baseUrl + "sync-delta?offset=" + String(_count));
        if (http.GET() == 200) {
            WiFiClient* stream = http.getStreamPtr();
            File f = LittleFS.open(_dbPath, "a");
            while (http.connected() && stream->available()) {
                String line = stream->readStringUntil('\n');
                if (line.length() > 5) {
                    f.println(line);
                    // Update RAM map on the fly
                    StaticJsonDocument<256> doc;
                    deserializeJson(doc, line);
                    _rfidMap[doc["t"].as<String>()] = doc["i"].as<String>();
                    _count++;
                }
            }
            f.close();
            // We assume version update to server's latest is handled by logic or provided in header
            // For simplicity, we'll re-run sync-check to get the final version number
            updateMetadataFromServer(); 
        }
        http.end();
    }

    void fetchFull() {
        Serial.println("Sync: Fetching Full Database (Rewrite)...");
        HTTPClient http;
        http.begin(_baseUrl + "sync-full");
        if (http.GET() == 200) {
            File f = LittleFS.open(_dbPath, "w");
            http.writeToStream(&f);
            f.close();
            loadDatabase(); // Refresh RAM map
            updateMetadataFromServer();
        }
        http.end();
    }

    void updateMetadataFromServer() {
        HTTPClient http;
        http.begin(_baseUrl + "sync-check");
        if (http.GET() == 200) {
            StaticJsonDocument<128> doc;
            deserializeJson(doc, http.getString());
            _version = doc["v"];
            _count = doc["c"];
            savePtr();
        }
        http.end();
    }

    // Add new tag locally and push to server
    bool registerNewTag(String tag) {
        HTTPClient http;
        http.begin(_baseUrl + "generate?tag=" + tag);
        int code = http.GET();
        if (code == 200) {
            String payload = http.getString(); // Expects JSON: {"t":"TAG","i":"S101","v":45}
            StaticJsonDocument<256> doc;
            deserializeJson(doc, payload);
            
            appendLocal(doc["t"], doc["i"]);
            _version = doc["v"];
            savePtr();
            return true;
        }
        http.end();
        return false;
    }
};

#endif