#ifndef ASYNC_HTTP_CLIENT_H
#define ASYNC_HTTP_CLIENT_H

#include <Arduino.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <functional>

class AsyncHttpClient {
public:
	// Callback signature: passes the payload (String) and whether it succeeded (bool)
	typedef std::function<void(String payload, bool success)> HttpCallback;

private:
	AsyncClient* aClient = nullptr;
	String requestHost = "";
	String requestPath = "";
	uint16_t requestPort = 80;
	String requestUrl = "";
	String httpResponse = "";

	// Volatile because these are modified by the AsyncTCP background task
	volatile bool requestFinished = false;
	volatile bool requestSuccess = false;
	volatile bool clientNeedsCleanup = false;

	HttpCallback onCompleteCallback = nullptr;

	uint32_t requestTimeoutMs = 10000; // Default to 10 seconds
	uint32_t requestStartTime = 0;

public:
	AsyncHttpClient() {}

	~AsyncHttpClient() {
		closeConnection();
	}

	// ==========================================
	// --- API Methods ---
	// ==========================================

	void setTimeout(uint32_t timeoutMs) {
		requestTimeoutMs = timeoutMs;
	}
	void onComplete(HttpCallback cb) {
		onCompleteCallback = cb;
	}

	bool isBusy() const {
		return aClient != nullptr || clientNeedsCleanup;
	}

	bool isFinished() const {
		return requestFinished;
	}

	bool isSuccess() const {
		return requestSuccess;
	}

	// Clears the finished state so you can poll it safely without re-triggering
	void resetState() {
		requestFinished = false;
		requestSuccess = false;
	}

	// Separate Payload Parsing function
	String getPayload() const {
		if (httpResponse.length() == 0) return "";

		int bodyIndex = httpResponse.indexOf("\r\n\r\n");
		String payload = "";

		if (bodyIndex != -1) {
			payload = httpResponse.substring(bodyIndex + 4);
		} else {
			payload = httpResponse; // Fallback if headers are missing
		}

		payload.trim();
		return payload;
	}

	// Get the raw response (headers + payload)
	String getRawResponse() const {
		return httpResponse;
	}

	bool GET(String url) {
		if (WiFi.status() != WL_CONNECTED) {
			Serial.println("AsyncHttpClient Error: WiFi not connected!");
			return false;
		}

		if (isBusy()) {
			Serial.println("AsyncHttpClient Error: Request already in progress.");
			return false;
		}

		requestUrl = url;
		parseUrl(url); // Sets Host, Path, and Port

		// Reset all states for the new request
		httpResponse = "";
		requestFinished = false;
		requestSuccess = false;
		clientNeedsCleanup = false;

		requestStartTime = millis();

		aClient = new AsyncClient();
		aClient->onConnect(_onConnect, this);
		aClient->onData(_onData, this);
		aClient->onDisconnect(_onDisconnect, this);
		aClient->onError(_onError, this);
		aClient->onTimeout(_onTimeout, this);

		Serial.printf("AsyncTCP: Connecting to %s on port %d (Path: %s)...\n",
				requestHost.c_str(), requestPort, requestPath.c_str());

		return aClient->connect(requestHost.c_str(), requestPort);
	}

public:
	void closeConnection(){
		if (aClient) {
			// Unbind to prevent rogue callbacks during deletion
			aClient->onConnect(NULL, NULL);
			aClient->onData(NULL, NULL);
			aClient->onDisconnect(NULL, NULL);
			aClient->onError(NULL, NULL);
			aClient->onTimeout(NULL, NULL);

			// Abort instantly frees the underlying LwIP socket
			aClient->abort();
			abortrequested=true;
			aClient->close(true);
			/*delete aClient;
			aClient = nullptr;*/
		}

	}
	bool abortrequested=false;
	// Must be called continuously in your main loop() to clean up memory
	void update() {
 		if (!abortrequested && aClient != nullptr && !requestFinished && !clientNeedsCleanup) {
			if (millis() - requestStartTime > requestTimeoutMs) {
				Serial.println("AsyncHttpClient: Software Timeout Reached! Forcing disconnect.");
				requestSuccess = false;
				requestFinished = true;
				clientNeedsCleanup = true;
			}
		}
		if (clientNeedsCleanup) {
			clientNeedsCleanup = false;
			closeConnection();
			// If a callback was provided, trigger it automatically
			if (onCompleteCallback) {
				onCompleteCallback(getPayload(), requestSuccess);
				resetState();// Reset state automatically if using callbacks so it doesn't fire twice
			}
		}
	}



	// ==========================================
	// --- Helper: URL Parsing ---
	// ==========================================
	void parseUrl(const String& url) {
		String tempUrl = url;

		// Strip "http://" if present
		if (tempUrl.startsWith("http://")) {
			tempUrl = tempUrl.substring(7);
		} else if (tempUrl.startsWith("https://")) {
			Serial.println("AsyncHttpClient Error: HTTPS is not natively supported by basic AsyncTCP.");
		}

		// Split Host/Port from the Path
		int slashIndex = tempUrl.indexOf('/');
		String hostPort = tempUrl;
		requestPath = "/";
		if (slashIndex != -1) {
			hostPort = tempUrl.substring(0, slashIndex);
			requestPath = tempUrl.substring(slashIndex);
		}

		// Split Host from the Port
		int colonIndex = hostPort.indexOf(':');
		if (colonIndex != -1) {
			requestHost = hostPort.substring(0, colonIndex);
			requestPort = hostPort.substring(colonIndex + 1).toInt();
		} else {
			requestHost = hostPort;
			requestPort = 80; // Default HTTP port
		}
	}

	// ==========================================
	// --- AsyncTCP Static Callbacks ---
	// ==========================================
	static void _onConnect(void* arg, AsyncClient* client) {
		AsyncHttpClient* instance = (AsyncHttpClient*)arg;

		String requestHeader = "GET " + instance->requestPath + " HTTP/1.1\r\n";
		requestHeader += "Host: " + instance->requestHost + "\r\n";
		requestHeader += "Connection: close\r\n\r\n";

		client->write(requestHeader.c_str());

		instance->requestSuccess = true;
	}

	static void _onData(void* arg, AsyncClient* client, void* data, size_t len) {
		AsyncHttpClient* instance = (AsyncHttpClient*)arg;
		char* str = (char*)data;

		char* buf = (char*)malloc(len + 1);
		if (buf) {
			memcpy(buf, data, len);
			buf[len] = '\0';
			instance->httpResponse += buf;
			free(buf);
		}

	}

	static void _onDisconnect(void* arg, AsyncClient* client) {
		Serial.println(String()+"_onDisconnect client:"+(unsigned long)client+" arg:"+(unsigned long)arg);
		AsyncHttpClient* instance = (AsyncHttpClient*)arg;
		if(!instance) return;
		Serial.println(String()+"_onDisconnect url:"+instance->requestUrl);
		//instance->requestSuccess = true;
		instance->requestFinished = true;
		instance->clientNeedsCleanup = true; // Signal update loop to delete client
//		Serial.println(String()+"_onDisconnect preclose ");
//		instance->closeConnection();
//		Serial.println(String()+"_onDisconnect postclose ");
	}

	static void _onError(void* arg, AsyncClient* client, int8_t error) {
		Serial.println(String()+"_onError client:"+(unsigned long)client+" error:"+error+" arg:"+(unsigned long)arg);
		AsyncHttpClient* instance = (AsyncHttpClient*)arg;
		Serial.println("AsyncHttpClient: _onError url:" + instance->requestUrl);
//		Serial.printf("AsyncHttpClient: Error %d on %s\n", error, instance->requestUrl.c_str());
		instance->requestSuccess = false;
//		instance->requestFinished = true;
//		instance->clientNeedsCleanup = true;
//		Serial.println(String()+"_onError preclose ");
//		instance->closeConnection();
//		Serial.println(String()+"_onError postclose ");
	}

	static void _onTimeout(void* arg, AsyncClient* client, uint32_t time) {
		Serial.println(String()+"_onTimeout client:"+(unsigned long)client+" time:"+time+" arg:"+(unsigned long)arg);
		AsyncHttpClient* instance = (AsyncHttpClient*)arg;
		Serial.println("AsyncHttpClient: Timeout url:" + instance->requestUrl);
		instance->requestSuccess = false;
		instance->requestFinished = true;
		instance->clientNeedsCleanup = true;
//		Serial.println(String()+"_onTimeout preclose ");
//		instance->closeConnection();
//		Serial.println(String()+"_onTimeout postclose ");
	}

};

#endif


/*
Usage Example: Using Getters (Polling Method)

If you don't want to use callbacks, you can just poll isFinished() in your
loop(). This is often cleaner for complex State Machines.

#include "AsyncHttpClient.h"

AsyncHttpClient httpClient;

void setup() {
    Serial.begin(115200);
    // ... connect to WiFi ...
}

void loop() {
    // 1. MUST call update() to manage memory safely
    httpClient.update();

    // 2. Poll the state
    if (httpClient.isFinished()) {

        if (httpClient.isSuccess()) {
            // Extract the payload dynamically
            String data = httpClient.getPayload();
            Serial.println("Data received: " + data);

            // Trigger your buzzer / ID loaders here
            // urlLoaded(data);
            // vbeep->playSpecial("agree");
        } else {
            Serial.println("Request Failed!");
        }

        // 3. Clear the state so this block doesn't keep running continuously
        httpClient.resetState();
    }

    // Example of triggering the GET request
    if (/* some button pressed * / && !httpClient.isBusy()) {
        httpClient.GET("http://192.168.1.143/generateID");
    }
}
 */
