#ifndef ASYNCHTTPREQUEST_H
#define ASYNCHTTPREQUEST_H
#include <vector>
#include <utility>
#include <Arduino.h>
#include <StreamString.h>
#include <WiFi.h>
#include <FS.h>

// Set to 1 to enable the custom FreeRTOS watchdog timeout, 0 to disable it.
#ifndef DIY_TIMEOUT
#define DIY_TIMEOUT 1
#endif

#include <freertos/FreeRTOS.h>
#include <freertos/timers.h>
#include <freertos/semphr.h>
#include <esp_system.h>

#ifndef DEL_TASK_STACK_SIZE
#define DEL_TASK_STACK_SIZE 4096
#endif

// Set to 1 to enable task stack and heap memory diagnostics in Serial monitor, 0 to disable
#ifndef DEBUG_ASYNC_HTTP_MEMORY
#define DEBUG_ASYNC_HTTP_MEMORY 0
#endif

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

#include <functional>
#include <string>

#include "debugtools.h"

#define TIMEOUT_MS 10000

class AsyncHttpRequest {
private:
	AsyncHTTPRequest _request;
	String _url;
	String _method;
	String _postBody;
	std::vector<std::pair<String, String>> _headers;
	unsigned long _timeout = TIMEOUT_MS;

	StreamString *_content = nullptr;
	File _destFile;
	String _errorMsg;

	std::function<void(const StreamString *,AsyncHttpRequest*)> _onSuccess;
	std::function<void(const String &,AsyncHttpRequest*)> _onFailure;

	volatile bool _hasError = false;
	volatile bool _isFinished = false;
	bool _streamToFile = false;

	// --- Lifecycle Thread Safety ---
	portMUX_TYPE _lifecycleMux = portMUX_INITIALIZER_UNLOCKED;
	volatile bool _callbacksClaimed = false;
	volatile bool _callbacksRunning = false;
	volatile bool _deleteClaimed = false;
	SemaphoreHandle_t _cbDoneSem = xSemaphoreCreateBinary();

	bool claimCallbacks() {
		bool claimed = false;
		portENTER_CRITICAL(&_lifecycleMux);
		if (!_callbacksClaimed) {
			_callbacksClaimed = true;
			_callbacksRunning = true; // Lock the execution state
			claimed = true;
		}
		portEXIT_CRITICAL(&_lifecycleMux);
		return claimed;
	}

	bool claimDelete() {
		bool claimed = false;
		portENTER_CRITICAL(&_lifecycleMux);
		if (!_deleteClaimed) {
			_deleteClaimed = true;
			claimed = true;
		}
		portEXIT_CRITICAL(&_lifecycleMux);
		return claimed;
	}

#if DIY_TIMEOUT
	// --- Custom Watchdog timeout support ---
	TimerHandle_t _timeoutTimer = nullptr;

	void startTimeoutTimer() {
		TickType_t timerTicks = pdMS_TO_TICKS(_timeout);
		if (timerTicks == 0) timerTicks = 1;

		_timeoutTimer = xTimerCreate("httpReqTO", timerTicks, pdFALSE, this, timeoutTimerCB);
		if (_timeoutTimer) {
			xTimerStart(_timeoutTimer, 0);
		}
	}

	void stopTimeoutTimer() {
		if (_timeoutTimer) {
			xTimerStop(_timeoutTimer, 0);
			xTimerDelete(_timeoutTimer, 0);
			_timeoutTimer = nullptr;
		}
	}

	static void timeoutTimerCB(TimerHandle_t timer) {
		AsyncHttpRequest *self = static_cast<AsyncHttpRequest*>(pvTimerGetTimerID(timer));
		if (self) self->handleTimeout();
	}

	void handleTimeout() {
		Serial.println(String() + "[AsyncHttpRequest] Watchdog Timeout after " + _timeout + "ms for url:" + _url);

		_hasError = true;
		_errorMsg = "Request timed out";

		// Tell TCP to start shutting down (this might take time to actually disconnect)
		_request.abort();

		// Fire callbacks IMMEDIATELY so the application (FSM) can move on!
		if (claimCallbacks()) {
			if (xTaskCreate(deferredCallbackTask, "cbTask", 8192, this, 1, NULL) != pdPASS) {
				_callbacksRunning = false; // Fallback if OOM to prevent deadlock
			}
		}
	}
#endif

	void closeDestFile() {
		if (_streamToFile && _destFile) {
			_destFile.close();
		}
	}

	static void readyStateChangeCB(void* optParm, AsyncHTTPRequest* req, int readyState) {
		if (optParm) {
			DEBUG_LOG_CB("readyStateChangeCB %p state:%d task:%s", (void*)optParm, readyState, pcTaskGetName(NULL));
			static_cast<AsyncHttpRequest*>(optParm)->handleReadyState(readyState);
			DEBUG_LOG_CB("readyStateChangeCB out");
		}
	}

	static void dataCB(void* optParm, AsyncHTTPRequest* req, size_t availableBytes) {
		if (optParm) {
			DEBUG_LOG_CB("dataCB %p avail:%d task:%s", (void*)optParm, (int)availableBytes, pcTaskGetName(NULL));
			static_cast<AsyncHttpRequest*>(optParm)->handleData();
			DEBUG_LOG_CB("dataCB out");
		}
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
		Serial.println("[CB] handleReadyState ENTER");
		if (readyState == readyStateHdrsRecvd || readyState == 3) {
			handleData();
		}

		// The ONLY safe place to destroy the object: when AsyncTCP natively finishes.
		if (readyState == readyStateDone) {
#if DIY_TIMEOUT
			stopTimeoutTimer();
#endif
			int responseCode = _request.responseHTTPcode();
			if (!_hasError && (responseCode < 200 || responseCode >= 300) && responseCode != 0) {
				_hasError = true;
				_errorMsg = String("HTTP error ") + responseCode;
			}

			handleData();

			 closeDestFile();
			_isFinished = true;
			_request.abort();

			// If the watchdog DID NOT fire, we must fire callbacks now
			if (claimCallbacks()) {
				if (xTaskCreate(deferredCallbackTask, "cbTask", 8192, this, 1, NULL) != pdPASS) {
					_callbacksRunning = false;
				}
			}

			// Regardless of how we finished, trigger safe deletion now
			if (claimDelete()) {
			//	_request.abort();
				xTaskCreate(deferredDeleteTask, "delTask", DEL_TASK_STACK_SIZE, this, 1, NULL);
			}
		}
		Serial.println("[CB] handleReadyState EXIT");
	}

	// --- Lifecycle Tasks ---

	static void deferredCallbackTask(void* param) {
		AsyncHttpRequest* req = static_cast<AsyncHttpRequest*>(param);

#if DEBUG_HTTP_REQUESTS
		if (!req->_hasError) {
			size_t contentSize = req->_streamToFile ? 0 : (req->_content ? req->_content->length() : 0);
			if (contentSize > 0 && contentSize <= 200) {
				String content = req->_content->c_str();
				content.trim();
				Serial.printf("[HTTP_RES] %p SUCCESS (%d bytes): %s\n", (void*)req, contentSize, content.c_str());
			} else {
				Serial.printf("[HTTP_RES] %p SUCCESS (%d bytes)\n", (void*)req, contentSize);
			}
		} else {
			Serial.printf("[HTTP_RES] %p ERROR: %s\n", (void*)req, req->_errorMsg.c_str());
		}
#endif

		if (!req->_hasError) {
			if (req->_onSuccess) req->_onSuccess(req->_streamToFile ? nullptr : req->_content, req);
		} else {
			if (req->_onFailure) req->_onFailure(req->_errorMsg.isEmpty() ? "HTTP request failed" : req->_errorMsg, req);
		}

		// Clear callback std::function to destroy captures inside cbTask's 8KB stack
		req->_onSuccess = nullptr;
		req->_onFailure = nullptr;

#if DEBUG_ASYNC_HTTP_MEMORY
		UBaseType_t cbWatermark = uxTaskGetStackHighWaterMark(NULL);
		Serial.printf("[cbTask] Finished callbacks. HighWaterMark: %u bytes remaining (Peak stack used: %u / 8192 bytes)\n",
			(unsigned int)cbWatermark, (unsigned int)(8192 - cbWatermark));
#endif

		req->_callbacksRunning = false;
		if (req->_cbDoneSem) {
			xSemaphoreGive(req->_cbDoneSem);
		}
		vTaskDelete(NULL);
	}

	static void deferredDeleteTask(void* param) {
		AsyncHttpRequest* req = static_cast<AsyncHttpRequest*>(param);
		Serial.printf("[delTask] ENTER for %p\n", (void*)req);

#if DEBUG_ASYNC_HTTP_MEMORY
		UBaseType_t startWatermark = uxTaskGetStackHighWaterMark(NULL);
		Serial.printf("[delTask] Started. Allocated stack: %u bytes. HighWaterMark: %u bytes remaining. sizeof(AsyncHttpRequest): %u bytes. Free heap: %u bytes\n",
			(unsigned int)DEL_TASK_STACK_SIZE,
			(unsigned int)startWatermark,
			(unsigned int)sizeof(AsyncHttpRequest),
			(unsigned int)esp_get_free_heap_size());
#endif

		// 1. Wait until user callbacks are completely finished via semaphore
		if (req->_callbacksClaimed && req->_cbDoneSem) {
			xSemaphoreTake(req->_cbDoneSem, pdMS_TO_TICKS(15000));
		} else {
			while (req->_callbacksRunning) {
				vTaskDelay(pdMS_TO_TICKS(10));
			}
		}

		// 2. Wait to ensure cbTask has completely exited vTaskDelete(NULL) and AsyncTCP has returned
		vTaskDelay(pdMS_TO_TICKS(100));

#if DEBUG_ASYNC_HTTP_MEMORY
		UBaseType_t beforeDelWatermark = uxTaskGetStackHighWaterMark(NULL);
		Serial.printf("[delTask] About to delete req. HighWaterMark before delete: %u bytes remaining (Peak stack used so far: %u / %u bytes)\n",
			(unsigned int)beforeDelWatermark,
			(unsigned int)(DEL_TASK_STACK_SIZE - beforeDelWatermark),
			(unsigned int)DEL_TASK_STACK_SIZE);
#endif

		Serial.printf("[delTask] About to delete %p\n", (void*)req);
		delete req;
		Serial.printf("[delTask] After delete %p\n", (void*)req);

#if DEBUG_ASYNC_HTTP_MEMORY
		UBaseType_t afterDelWatermark = uxTaskGetStackHighWaterMark(NULL);
		Serial.printf("[delTask] Finished delete req. HighWaterMark: %u bytes remaining (Peak stack used: %u / %u bytes). Free heap: %u bytes\n",
			(unsigned int)afterDelWatermark,
			(unsigned int)(DEL_TASK_STACK_SIZE - afterDelWatermark),
			(unsigned int)DEL_TASK_STACK_SIZE,
			(unsigned int)esp_get_free_heap_size());
#endif

		Serial.printf("[delTask] EXIT\n");
		vTaskDelete(NULL);
	}

	void abortAndCleanup(const String& msg) {
		_hasError = true;
		_errorMsg = msg;
		closeDestFile();
		if (claimCallbacks()) {
			if (xTaskCreate(deferredCallbackTask, "cbTask", 8192, this, 1, NULL) != pdPASS) _callbacksRunning = false;
		}
		if (claimDelete()) {
		//	_request.abort();
			xTaskCreate(deferredDeleteTask, "delTask", DEL_TASK_STACK_SIZE, this, 1, NULL);
		}
	}

public:
	AsyncHttpRequest(const String &url,
			std::function<void(const StreamString *,AsyncHttpRequest*)> onSuccess,
			std::function<void(const String &,AsyncHttpRequest*)> onFailure, unsigned long timeout0=TIMEOUT_MS)
	: _onSuccess(onSuccess), _onFailure(onFailure), _timeout(timeout0) {
		_method = "GET";
		_url = url;
		_content = new StreamString();
		_content->reserve(4096);
#if DEBUG_HTTP_REQUESTS
		Serial.printf("[HTTP_REQ] %p GET %s\n", (void*)this, _url.c_str());
#endif
		DEBUG_LOG_LIFECYCLE("CTOR %p task:%s URL: %s", (void*)this, pcTaskGetName(NULL), _url.c_str());
	}

	AsyncHttpRequest(const String &url, const String &postBody,
			std::function<void(const StreamString *,AsyncHttpRequest*)> onSuccess,
			std::function<void(const String &,AsyncHttpRequest*)> onFailure, unsigned long timeout0=TIMEOUT_MS)
	: _postBody(postBody), _onSuccess(onSuccess), _onFailure(onFailure), _timeout(timeout0) {
		_method = "POST";
		_url = url;
		_content = new StreamString();
		_content->reserve(4096);
#if DEBUG_HTTP_REQUESTS
		Serial.printf("[HTTP_REQ] %p POST %s (body: %d bytes)\n", (void*)this, _url.c_str(), _postBody.length());
#endif
	}

	AsyncHttpRequest(const String &url, File destFile,
			std::function<void(const StreamString *,AsyncHttpRequest*)> onSuccess,
			std::function<void(const String &,AsyncHttpRequest*)> onFailure, unsigned long timeout0=TIMEOUT_MS)
	: _destFile(destFile), _onSuccess(onSuccess), _onFailure(onFailure), _streamToFile(true), _timeout(timeout0) {
		_method = "GET";
		_url = url;
#if DEBUG_HTTP_REQUESTS
		Serial.printf("[HTTP_REQ] %p GET %s (to file)\n", (void*)this, _url.c_str());
#endif
	}

	AsyncHttpRequest(const String &method, const String &url, const String &body,
			const std::vector<std::pair<String, String>> &headers,
			std::function<void(const StreamString *,AsyncHttpRequest*)> onSuccess,
			std::function<void(const String &,AsyncHttpRequest*)> onFailure, unsigned long timeout0=TIMEOUT_MS)
	: _method(method), _url(url), _postBody(body), _headers(headers), _onSuccess(onSuccess), _onFailure(onFailure), _timeout(timeout0) {
		_content = new StreamString();
		_content->reserve(4096);
#if DEBUG_HTTP_REQUESTS
		Serial.printf("[HTTP_REQ] %p %s %s (body: %d bytes, headers: %d)\n", (void*)this, _method.c_str(), _url.c_str(), _postBody.length(), _headers.size());
#endif
	}

	~AsyncHttpRequest() {
		DEBUG_LOG_LIFECYCLE("DTOR %p task:%s", (void*)this, pcTaskGetName(NULL));
#if DEBUG_ASYNC_HTTP_MEMORY
		UBaseType_t dtorWatermark = uxTaskGetStackHighWaterMark(NULL);
		Serial.printf("[~AsyncHttpRequest] Destructor in task '%s'. Stack remaining: %u bytes. Free heap: %u bytes\n",
			pcTaskGetName(NULL),
			(unsigned int)dtorWatermark,
			(unsigned int)esp_get_free_heap_size());
#endif
#if DIY_TIMEOUT
		stopTimeoutTimer();
#endif
		//_request.abort();
		if (_content) {
			delete _content;
			_content = nullptr;
		}
		if (_cbDoneSem) {
			vSemaphoreDelete(_cbDoneSem);
			_cbDoneSem = nullptr;
		}
	}

	String getUrl() { return _url; }
	int responseHTTPcode() {return _request.responseHTTPcode();}

	void begin() {
		_request.onReadyStateChange(readyStateChangeCB, this);
		_request.onData(dataCB, this);

	//	Serial.println(String()+"AsyncHttpRequest::begin _method:"+_method);

		if (!_request.open(_method.c_str(), _url.c_str())) {
			abortAndCleanup("Failed to open connection");
			return;
		}

		uint32_t secs = _timeout / 1000;
		if (secs == 0) secs = 1;
		_request.setTimeout(secs);

#if DIY_TIMEOUT
		startTimeoutTimer();
#endif

		for (const auto &header : _headers) {
			_request.setReqHeader(header.first.c_str(), header.second.c_str());
		}

		if (_method == "POST" && _headers.empty()) {
			_request.setReqHeader("Content-Type", "application/octet-stream");
			_request.setReqHeader("Content-Length", String(_postBody.length()).c_str());
		}

		bool sent;
		if (_postBody.length() > 0) sent = _request.send((const uint8_t*)_postBody.c_str(),_postBody.length());
		else sent = _request.send();

//		Serial.println(String()+"AsyncHttpRequest::begin sent:"+sent+" _postBody:"+_postBody);

		if (!sent) {
#if DIY_TIMEOUT
			stopTimeoutTimer();
#endif
			abortAndCleanup("Failed to send request");
			return;
		}
	}

	static String getPayload(String httpResponse) {
		if (httpResponse.length() == 0) return "";
		int bodyIndex = httpResponse.indexOf("\r\n\r\n");
		if (bodyIndex != -1) {
			return httpResponse.substring(bodyIndex + 4);
		}
		return httpResponse;
	}

	static void fetch(const String &url,
			std::function<void(const StreamString *, AsyncHttpRequest*)> onSuccess,
			std::function<void(const String &, AsyncHttpRequest*)> onFailure, unsigned long timeout0=TIMEOUT_MS) {
		AsyncHttpRequest *request = new AsyncHttpRequest(url, onSuccess, onFailure, timeout0);
		request->begin();
	}

	static void post(const String &url, const String &body,
			std::function<void(const StreamString *, AsyncHttpRequest*)> onSuccess,
			std::function<void(const String &, AsyncHttpRequest*)> onFailure, unsigned long timeout0=TIMEOUT_MS) {
		AsyncHttpRequest *request = new AsyncHttpRequest(url, body, onSuccess, onFailure, timeout0);
		request->begin();
	}

	static void fetchToFile(const String &url, File destFile,
			std::function<void(const StreamString *, AsyncHttpRequest*)> onSuccess,
			std::function<void(const String &, AsyncHttpRequest*)> onFailure, unsigned long timeout0=TIMEOUT_MS) {
		if (!destFile) {
			if (onFailure) onFailure("Failed to open file for writing", nullptr);
			return;
		}
		AsyncHttpRequest *request = new AsyncHttpRequest(url, destFile, onSuccess, onFailure, timeout0);
		request->begin();
	}

	static void request(const String &method, const String &url, const String &body,
				const std::vector<std::pair<String, String>> &headers,
				std::function<void(const StreamString *, AsyncHttpRequest*)> onSuccess,
				std::function<void(const String &, AsyncHttpRequest*)> onFailure, unsigned long timeout0=TIMEOUT_MS) {
			AsyncHttpRequest *req = new AsyncHttpRequest(method, url, body, headers, onSuccess, onFailure, timeout0);
			req->begin();
		}
};

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
