#ifndef ASYNCHTTPREQUEST_H
#define ASYNCHTTPREQUEST_H

#include <vector>
#include <list>
#include <utility>
#include <Arduino.h>
#include <StreamString.h>
#include <WiFi.h>
#include <FS.h>
#include <functional>
#include <string>

// Set to 1 to enable HTTP request/response logging in Serial monitor, 0 to disable
#ifndef DEBUG_HTTP_REQUESTS
#define DEBUG_HTTP_REQUESTS 1
#endif

#pragma warning( push )
#pragma warning( disable : 4101 )
#define ESP32 ARDUINO_ARCH_ESP32
#pragma warning( disable : 4101 )
#include <AsyncHTTPRequest_Generic.h>
#pragma warning( pop )

#include "debugtools.h"

#define TIMEOUT_MS 10000

// Maximum number of generic (unnamed) request objects allowed to be created
// before forcing unnamed requests to wait in the queue.
#define MAX_ANON_OBJECTS 3

class AsyncHttpRequest {
private:
    // --- SINGLE QUEUE ARCHITECTURE ---
    struct PendingRequest {
        String method;
        String url;
        String postBody;
        std::vector<std::pair<String, String>> headers;
        File destFile;
        std::function<void(const StreamString *, AsyncHttpRequest*)> onSuccess;
        std::function<void(const String &, AsyncHttpRequest*)> onFailure;
        unsigned long timeout;
        String reqName; // Optional identifier
    };

    static std::list<PendingRequest>& getQueue() {
        static std::list<PendingRequest> pendingQueue;
        return pendingQueue;
    }

    static std::vector<AsyncHttpRequest*>& getPool() {
        static std::vector<AsyncHttpRequest*> pool;
        return pool;
    }

    // --- INSTANCE VARIABLES ---
    AsyncHTTPRequest _request;
    String _name; // Identity of this object (empty if anonymous)
    String _url;
    String _method;
    String _postBody;
    std::vector<std::pair<String, String>> _headers;
    unsigned long _timeout = TIMEOUT_MS;

    StreamString *_content = nullptr;
    File _destFile;
    String _errorMsg;

    std::function<void(const StreamString *, AsyncHttpRequest*)> _onSuccess;
    std::function<void(const String &, AsyncHttpRequest*)> _onFailure;

    volatile bool _hasError = false;
    volatile bool _isFinished = false;
    volatile bool _isReadyToProcess = false;

    bool _streamToFile = false;
    bool _inUse = false;
    unsigned long _startTime = 0;

    // Private constructor (enforces usage via static methods)
    AsyncHttpRequest() {
        _content = new StreamString();
        _content->reserve(4096);
    }

    // Configures and starts the HTTP request from PendingRequest data
    void configureAndBegin(const PendingRequest& reqData) {
        _method = reqData.method;
        _url = reqData.url;
        _postBody = reqData.postBody;
        _headers = reqData.headers;
        _destFile = reqData.destFile;
        _streamToFile = (reqData.destFile == true);

        _onSuccess = reqData.onSuccess;
        _onFailure = reqData.onFailure;
        _timeout = reqData.timeout;

        _hasError = false;
        _errorMsg = "";
        _isFinished = false;
        _isReadyToProcess = false;
        _inUse = true;

        if (_content) {
            _content->clear();
        } else {
            _content = new StreamString();
            _content->reserve(4096);
        }

#if DEBUG_HTTP_REQUESTS
        String printName = _name.isEmpty() ? "ANON" : _name;
        Serial.printf("[HTTP_REQ] %p [%s] %s %s\n", (void*)this, printName.c_str(), _method.c_str(), _url.c_str());
#endif

        _request.onReadyStateChange(readyStateChangeCB, this);
        _request.onData(dataCB, this);

        if (!_request.open(_method.c_str(), _url.c_str())) {
            _hasError = true;
            _errorMsg = "Failed to open connection";
            _isReadyToProcess = true;
            return;
        }

        uint32_t secs = _timeout / 1000;
        if (secs == 0) secs = 1;
        _request.setTimeout(secs);

        for (const auto &header : _headers) {
            _request.setReqHeader(header.first.c_str(), header.second.c_str());
        }

        if (_method == "POST" && _headers.empty()) {
            _request.setReqHeader("Content-Type", "application/octet-stream");
            _request.setReqHeader("Content-Length", String(_postBody.length()).c_str());
        }

        bool sent;
        if (_postBody.length() > 0) sent = _request.send((const uint8_t*)_postBody.c_str(), _postBody.length());
        else sent = _request.send();

        if (!sent) {
            _hasError = true;
            _errorMsg = "Failed to send request";
            _isReadyToProcess = true;
            return;
        }

        _startTime = millis();
    }

    // --- ASYNC CALLBACKS ---
    static void readyStateChangeCB(void* optParm, AsyncHTTPRequest* req, int readyState) {
        if (optParm) static_cast<AsyncHttpRequest*>(optParm)->handleReadyState(readyState);
    }

    static void dataCB(void* optParm, AsyncHTTPRequest* req, size_t availableBytes) {
        if (optParm) static_cast<AsyncHttpRequest*>(optParm)->handleData();
    }

    void handleData() {
        if (_hasError || _isFinished) return;

        uint8_t buf[512];
        while (_request.available() > 0) {
            if (_hasError || _isFinished) break;

            size_t amount = _request.available();
            if (amount > sizeof(buf)) amount = sizeof(buf);

            size_t bytesRead = _request.responseRead(buf, amount);
            if (bytesRead > 0) {
                if (_streamToFile && _destFile) {
                    _destFile.write(buf, bytesRead);
                } else if (!_streamToFile && _content) {
                    _content->write(buf, bytesRead);
                }
            } else {
                break;
            }
        }
    }

    void handleReadyState(int readyState) {
        if (readyState == readyStateHdrsRecvd || readyState == 3) {
            handleData();
        }

        if (readyState == readyStateDone) {
            int responseCode = _request.responseHTTPcode();
            if (!_hasError && (responseCode < 200 || responseCode >= 300) && responseCode != 0) {
                _hasError = true;
                _errorMsg = String("HTTP error ") + responseCode;
            }

            handleData();

            _isFinished = true;
            _request.abort();
            _isReadyToProcess = true; // Signal main loop
        }
    }

    void processAndCleanup() {
        if (!_hasError) {
#if DEBUG_HTTP_REQUESTS
            String printName = _name.isEmpty() ? "ANON" : _name;
            size_t contentSize = _streamToFile ? 0 : (_content ? _content->length() : 0);
            Serial.printf("[HTTP_RES] %p [%s] SUCCESS (%d bytes)\n", (void*)this, printName.c_str(), contentSize);
#endif
            if (_onSuccess) _onSuccess(_streamToFile ? nullptr : _content, this);
        } else {
#if DEBUG_HTTP_REQUESTS
            String printName = _name.isEmpty() ? "ANON" : _name;
            Serial.printf("[HTTP_RES] %p [%s] ERROR: %s\n", (void*)this, printName.c_str(), _errorMsg.c_str());
#endif
            if (_onFailure) _onFailure(_errorMsg.isEmpty() ? "HTTP request failed" : _errorMsg, this);
        }

        if (_streamToFile && _destFile) _destFile.close();

        _onSuccess = nullptr;
        _onFailure = nullptr;
        _inUse = false; // Mark free so the queue can claim it
    }

    // --- QUEUE PROCESSOR ---
    static void processQueue() {
        auto it = getQueue().begin();

        while (it != getQueue().end()) {
            bool requestStarted = false;

            if (it->reqName.length() > 0) {
                // 1. THIS IS A NAMED REQUEST
                AsyncHttpRequest* targetObj = nullptr;

                // Look for the specific named object
                for (auto* req : getPool()) {
                    if (req->_name == it->reqName) {
                        targetObj = req;
                        break;
                    }
                }

                // If it doesn't exist, create it
                if (!targetObj) {
                    targetObj = new AsyncHttpRequest();
                    targetObj->_name = it->reqName;
                    getPool().push_back(targetObj);
                }

                // If it's free, start the request! If busy, it safely waits in queue.
                if (!targetObj->_inUse) {
                    targetObj->configureAndBegin(*it);
                    it = getQueue().erase(it); // Remove from queue, advance iterator
                    requestStarted = true;
                }

            } else {
                // 2. THIS IS AN UNNAMED REQUEST
                AsyncHttpRequest* targetObj = nullptr;
                int anonCount = 0;

                // Look for ANY free unnamed object
                for (auto* req : getPool()) {
                    if (req->_name.isEmpty()) {
                        anonCount++;
                        if (!req->_inUse && targetObj == nullptr) {
                            targetObj = req;
                        }
                    }
                }

                // If none are free, but we haven't hit the max limit, create a new one
                if (!targetObj && anonCount < MAX_ANON_OBJECTS) {
                    targetObj = new AsyncHttpRequest();
                    getPool().push_back(targetObj);
                }

                // If we got one, start it! If not (max limit reached), it waits in queue.
                if (targetObj && !targetObj->_inUse) {
                    targetObj->configureAndBegin(*it);
                    it = getQueue().erase(it);
                    requestStarted = true;
                }
            }

            if (!requestStarted) {
                ++it; // Move to next item in the queue
            }
        }
    }

public:
    ~AsyncHttpRequest() {
        if (_content) {
            delete _content;
            _content = nullptr;
        }
    }

    String getUrl() { return _url; }
    int responseHTTPcode() { return _request.responseHTTPcode(); }

    // ----------------------------------------------------------------------------------
    // SYSTEM LOOP: MUST BE CALLED REPEATEDLY IN THE ARDUINO void loop()
    // ----------------------------------------------------------------------------------
    static void loop() {
        unsigned long currentMillis = millis();
        bool queueNeedsProcessing = false;

        for (auto* req : getPool()) {
            if (!req->_inUse) continue;

            // 1. Process Timeout
            if (!req->_isReadyToProcess) {
                if (currentMillis - req->_startTime > req->_timeout) {
                    String printName = req->_name.isEmpty() ? "ANON" : req->_name;
                    Serial.println(String() + "[AsyncHttpRequest] Watchdog Timeout for [" + printName + "] url:" + req->_url);
                    req->_hasError = true;
                    req->_errorMsg = "Request timed out";
                    req->_request.abort();
                    req->_isReadyToProcess = true;
                }
            }

            // 2. Process Cleanup
            if (req->_isReadyToProcess) {
                req->_isReadyToProcess = false;
                req->processAndCleanup(); // Object becomes free here
                queueNeedsProcessing = true;
            }
        }

        // 3. Trigger next items in queue if resources freed up
        if (queueNeedsProcessing && !getQueue().empty()) {
            processQueue();
        }
    }

    // ----------------------------------------------------------------------------------
    // STATIC API (Added optional 'reqName' to the end)
    // ----------------------------------------------------------------------------------
    static void fetch(const String &url,
            std::function<void(const StreamString *, AsyncHttpRequest*)> onSuccess,
            std::function<void(const String &, AsyncHttpRequest*)> onFailure, const String& reqName = "",
            unsigned long timeout0 = TIMEOUT_MS) {

        getQueue().push_back({"GET", url, "", {}, File(), onSuccess, onFailure, timeout0, reqName});
        processQueue();
    }

    static void post(const String &url, const String &body,
            std::function<void(const StreamString *, AsyncHttpRequest*)> onSuccess,
            std::function<void(const String &, AsyncHttpRequest*)> onFailure, const String& reqName = "",
            unsigned long timeout0 = TIMEOUT_MS) {

        getQueue().push_back({"POST", url, body, {}, File(), onSuccess, onFailure, timeout0, reqName});
        processQueue();
    }

    static void fetchToFile(const String &url, File destFile,
            std::function<void(const StreamString *, AsyncHttpRequest*)> onSuccess,
            std::function<void(const String &, AsyncHttpRequest*)> onFailure, const String& reqName = "",
            unsigned long timeout0 = TIMEOUT_MS) {

        if (!destFile) {
            if (onFailure) onFailure("Failed to open file for writing", nullptr);
            return;
        }
        getQueue().push_back({"GET", url, "", {}, destFile, onSuccess, onFailure, timeout0, reqName});
        processQueue();
    }

    static void request(const String &method, const String &url, const String &body,
                const std::vector<std::pair<String, String>> &headers,
                std::function<void(const StreamString *, AsyncHttpRequest*)> onSuccess,
                std::function<void(const String &, AsyncHttpRequest*)> onFailure, const String& reqName = "",
                unsigned long timeout0 = TIMEOUT_MS) {

        getQueue().push_back({method, url, body, headers, File(), onSuccess, onFailure, timeout0, reqName});
        processQueue();
    }

    static String getPayload(String httpResponse) {
        if (httpResponse.length() == 0) return "";
        int bodyIndex = httpResponse.indexOf("\r\n\r\n");
        if (bodyIndex != -1) {
            return httpResponse.substring(bodyIndex + 4);
        }
        return httpResponse;
    }
};

// Extracted from original code
std::vector<std::pair<String, String>> buildPatchHeaders(const String& crc, size_t start, size_t end, size_t totalSize, size_t contentLength) {
    std::vector<std::pair<String, String>> headers;
    headers.push_back({"Content-Type", "message/byterange"});
    if (crc.length() > 0) {
        headers.push_back({"If-Match", crc});
    }
    String rangeStr = "bytes " + String(start) + "-" + String(end) + "/" + String(totalSize);
    headers.push_back({"Content-Range", rangeStr});
    headers.push_back({"Content-Length", String(contentLength)});
    return headers;
}

#endif
