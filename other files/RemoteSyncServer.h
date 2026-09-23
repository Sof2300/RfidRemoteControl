#ifndef REMOTESYNCSERVER_H
#define REMOTESYNCSERVER_H

#include <Arduino.h>
#include "fs/SimpleFS.h"
#include <WebServer.h>
#include <rom/crc.h>

// Lightweight server-side counterpart to RemoteSyncFile.
// The server keeps the canonical file and answers the client's sync requests,
// while the client retains the richer sync state machine.
class RemoteSyncServer {
protected:
    String syncFilename;

    void ensureFileExists() {
        if (syncFilename.length() == 0) return;
        SIMPLEFS.begin();
        if (!SIMPLEFS.exists(syncFilename)) {
            File f = SIMPLEFS.openFile(syncFilename.c_str(), "w");
            if (f) f.close();
        }
    }

    size_t getLocalSize() {
        File f = SIMPLEFS.openFile(syncFilename.c_str(), "r");
        if (!f) return 0;
        size_t sz = f.size();
        f.close();
        return sz;
    }

    String getLocalCRC(size_t limit) {
        File f = SIMPLEFS.openFile(syncFilename.c_str(), "r");
        if (!f) {
            char hex[9];
            snprintf(hex, sizeof(hex), "%08lx", (unsigned long)0);
            return String(hex);
        }

        const size_t BLOCK_SIZE = 1024;
        uint8_t buffer[BLOCK_SIZE];
        uint32_t crc = 0;
        size_t totalRead = 0;

        while (f.available() && totalRead < limit) {
            size_t toRead = BLOCK_SIZE;
            if (totalRead + toRead > limit) toRead = limit - totalRead;
            size_t bytesRead = f.read(buffer, toRead);
            if (bytesRead == 0) break;
            crc = crc32_le(crc, buffer, bytesRead);
            totalRead += bytesRead;
        }

        f.close();

        char hex[9];
        snprintf(hex, sizeof(hex), "%08lx", (unsigned long)crc);
        return String(hex);
    }

public:
    void init(String filename) {
        syncFilename = String("/") + filename;
        ensureFileExists();
        Serial.printf("[SYNC-SRV] initialized file=%s\n", syncFilename.c_str());
    }

    void begin(WebServer &server) {
        server.on("/sync_check", HTTP_GET, [this, &server]() {
            handleSyncCheck(server);
        });

        server.on("/sync_download", HTTP_GET, [this, &server]() {
            handleFileDownload(server);
        });

        server.on("/sync_push_delta", HTTP_POST, [this, &server]() {
            handlePushDelta(server);
        });
    }

    void handleSyncCheck(WebServer &server) {
        if (syncFilename.length() == 0) {
            server.send(500, "text/plain", "sync filename not initialized");
            return;
        }

        ensureFileExists();
        size_t serverSize = getLocalSize();
        String serverCRC = getLocalCRC(serverSize);

        String clientSizeArg = server.arg("size");
        String clientCRCArg = server.arg("crc");
        String clientTotalArg = server.arg("total_size");

        if (clientSizeArg.length() == 0) {
            server.send(400, "text/plain", "missing size parameter");
            return;
        }

        size_t clientSize = clientSizeArg.toInt();
        bool same = (clientSize == serverSize) && (clientCRCArg.length() > 0) && (clientCRCArg == serverCRC);

        if (same) {
            server.send(200, "text/plain", "UP_TO_DATE");
        } else {
            server.send(200, "text/plain", "PULL_DELTA:" + String(serverSize));
        }

        Serial.printf("[SYNC-SRV] sync_check clientSize=%u serverSize=%u clientCRC=%s serverCRC=%s\n",
                      (unsigned)clientSize,
                      (unsigned)serverSize,
                      clientCRCArg.c_str(),
                      serverCRC.c_str());
    }

    void handleFileDownload(WebServer &server) {
        ensureFileExists();
        File f = SIMPLEFS.openFile(syncFilename.c_str(), "r");
        if (!f) {
            server.send(404, "text/plain", "file not found");
            return;
        }

        server.sendHeader("Cache-Control", "no-cache");
        server.streamFile(f, "application/octet-stream");
        f.close();
    }

    void handlePushDelta(WebServer &server) {
        ensureFileExists();

        String offsetArg = server.arg("offset");
        size_t offset = offsetArg.length() ? offsetArg.toInt() : 0;
        size_t currentSize = getLocalSize();

        if (offset > currentSize) {
            server.send(409, "text/plain", "offset beyond current file size");
            return;
        }

        String payload = server.arg("plain");
        if (payload.length() == 0) {
            server.send(400, "text/plain", "empty delta payload");
            return;
        }

        File f = SIMPLEFS.openFile(syncFilename.c_str(), "a");
        if (!f) {
            server.send(500, "text/plain", "failed to open file for append");
            return;
        }

        size_t written = f.print(payload.c_str());
        f.close();

        size_t newSize = getLocalSize();
        Serial.printf("[SYNC-SRV] push_delta offset=%u bytes=%u newSize=%u\n",
                      (unsigned)offset,
                      (unsigned)written,
                      (unsigned)newSize);

        server.send(200, "text/plain", "OK");
    }
};

#endif
