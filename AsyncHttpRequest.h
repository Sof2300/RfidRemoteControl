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

#if DIY_TIMEOUT
#include <freertos/FreeRTOS.h>
#include <freertos/timers.h>
#endif

#pragma warning( push )
#pragma warning( disable : 4101 )
#define ESP32 ARDUINO_ARCH_ESP32
#pragma warning( disable : 4101 )
#include <AsyncHTTPRequest_Generic.h>
#pragma warning( pop )

#include <functional>
#include <string>

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
			static_cast<AsyncHttpRequest*>(optParm)->handleReadyState(readyState);
		}
	}

	static void dataCB(void* optParm, AsyncHTTPRequest* req, size_t availableBytes) {
		if (optParm) {
			static_cast<AsyncHttpRequest*>(optParm)->handleData();
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

			// We are natively in the AsyncTCP thread, so closing the file here is perfectly safe
			closeDestFile();
			_isFinished = true;

			// If the watchdog DID NOT fire, we must fire callbacks now
			if (claimCallbacks()) {
				if (xTaskCreate(deferredCallbackTask, "cbTask", 8192, this, 1, NULL) != pdPASS) {
					_callbacksRunning = false;
				}
			}

			// Regardless of how we finished, trigger safe deletion now
			if (claimDelete()) {
				xTaskCreate(deferredDeleteTask, "delTask", 2048, this, 1, NULL);
			}
		}
	}

	// --- Lifecycle Tasks ---

	static void deferredCallbackTask(void* param) {
		AsyncHttpRequest* req = static_cast<AsyncHttpRequest*>(param);

		if (!req->_hasError) {
			if (req->_onSuccess) req->_onSuccess(req->_streamToFile ? nullptr : req->_content, req);
		} else {
			if (req->_onFailure) req->_onFailure(req->_errorMsg.isEmpty() ? "HTTP request failed" : req->_errorMsg, req);
		}

		req->_callbacksRunning = false; // Unblock deletion task
		vTaskDelete(NULL);
	}

	static void deferredDeleteTask(void* param) {
		AsyncHttpRequest* req = static_cast<AsyncHttpRequest*>(param);

		// 1. Wait until user callbacks are completely finished to avoid deleting memory they are using
		while (req->_callbacksRunning) {
			vTaskDelay(pdMS_TO_TICKS(10));
		}

		// 2. Wait a tiny bit (50ms) to ensure AsyncTCP has completely returned
		//    from its readyStateChangeCB internal call tree.
		vTaskDelay(pdMS_TO_TICKS(50));

		delete req;
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
			xTaskCreate(deferredDeleteTask, "delTask", 2048, this, 1, NULL);
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
	}

	AsyncHttpRequest(const String &url, const String &postBody,
			std::function<void(const StreamString *,AsyncHttpRequest*)> onSuccess,
			std::function<void(const String &,AsyncHttpRequest*)> onFailure, unsigned long timeout0=TIMEOUT_MS)
	: _postBody(postBody), _onSuccess(onSuccess), _onFailure(onFailure), _timeout(timeout0) {
		_method = "POST";
		_url = url;
		_content = new StreamString();
		_content->reserve(4096);
	}

	AsyncHttpRequest(const String &url, File destFile,
			std::function<void(const StreamString *,AsyncHttpRequest*)> onSuccess,
			std::function<void(const String &,AsyncHttpRequest*)> onFailure, unsigned long timeout0=TIMEOUT_MS)
	: _destFile(destFile), _onSuccess(onSuccess), _onFailure(onFailure), _streamToFile(true), _timeout(timeout0) {
		_method = "GET";
		_url = url;
	}

	AsyncHttpRequest(const String &method, const String &url, const String &body,
			const std::vector<std::pair<String, String>> &headers,
			std::function<void(const StreamString *,AsyncHttpRequest*)> onSuccess,
			std::function<void(const String &,AsyncHttpRequest*)> onFailure, unsigned long timeout0=TIMEOUT_MS)
	: _method(method), _url(url), _postBody(body), _headers(headers), _onSuccess(onSuccess), _onFailure(onFailure), _timeout(timeout0) {
		_content = new StreamString();
		_content->reserve(4096);
	}

	~AsyncHttpRequest() {
#if DIY_TIMEOUT
		stopTimeoutTimer();
#endif
		if (_content) delete _content;
	}

	String getUrl() { return _url; }
	int responseHTTPcode() {return _request.responseHTTPcode();}

	void begin() {
		_request.onReadyStateChange(readyStateChangeCB, this);
		_request.onData(dataCB, this);

		Serial.println(String()+"AsyncHttpRequest::begin _method:"+_method);

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

		Serial.println(String()+"AsyncHttpRequest::begin sent:"+sent+" _postBody:"+_postBody);

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
