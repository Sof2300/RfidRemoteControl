#ifndef WIFIMANAGER_H
#define WIFIMANAGER_H
#include <WiFi.h>
#include <esp_wifi.h>
/*
const char* ssid = "Bamboo";
const char* password = "gingembre";
*/

bool connecting=false;
bool wificonnected=false;
bool connectionfailed=false;
String wifissid, wifipassword;

// Timing variables for WiFi management
unsigned long lastConnectionCheck = 0;
unsigned long connectionCheckInterval = 3000; // Check every 3 seconds
unsigned long lastPeriodicReconnect = 0;
unsigned long periodicReconnectInterval = 600000; // Reconnect every 10 minutes (600000ms)
unsigned long reconnectDelay = 5000; // 5 seconds delay for reconnection attempts
bool periodicReconnectEnabled = true; // Enable/disable periodic reconnects

// Forward declarations
const char* wifiStatusString(wl_status_t status);
const char* wifiDisconnectReason(uint8_t reason);

void wiFiGotIP(WiFiEvent_t event, WiFiEventInfo_t info){
	Serial.println("WiFi connected");
	Serial.println("IP address: ");
	Serial.println(IPAddress(info.got_ip.ip_info.ip.addr));
}
const uint8_t bssid[] = {0x1A, 0x2B, 0x3C, 0x4D, 0x5E, 0x6F};

void connect(String ssid, String password){
	if(ssid.length()==0 || password.length()==0) return;
	if(connecting) return;
	wifissid=ssid, wifipassword=password;
//
/*	WiFi.mode(WIFI_STA);
	WiFi.disconnect(false, false);   // do NOT erase config
	WiFi.setSleep(WIFI_PS_NONE);
	delay(5000);  */                   // wait 2 seconds
	WiFi.begin(wifissid, wifipassword);

    Serial.println(String()+"Connecting with ssid:'"+wifissid+"', password:'"+wifipassword+"'");
    connectionfailed=false;
    connecting=true;
}

int shouldreconnect=0;
unsigned reconnectts=0, waitreconnect=400;

bool loopWifi(){
	unsigned long currentTime = millis();

	// 1. Periodic connection check every 3 seconds
	if (currentTime - lastConnectionCheck >= connectionCheckInterval) {
		lastConnectionCheck = currentTime;

		// Check actual WiFi status, not just our cached state
		wl_status_t currentStatus = WiFi.status();
		bool actuallyConnected = (currentStatus == WL_CONNECTED);

		// Update our state if there's a mismatch
		if (actuallyConnected != wificonnected) {
			Serial.printf("[WiFi] Connection state mismatch: wificonnected=%d, actual=%d (%s)\n",
				wificonnected, actuallyConnected, wifiStatusString(currentStatus));

			if (actuallyConnected && !wificonnected) {
				// We are connected but didn't know it
				wificonnected = true;
				connecting = false;
				connectionfailed = false;
				Serial.println("[WiFi] Recovered connection state");
			} else if (!actuallyConnected && wificonnected) {
				// We think we're connected but we're not
				wificonnected = false;
				connectionfailed = true;
				Serial.println("[WiFi] Connection lost (detected by periodic check)");
				// Trigger reconnection
				reconnectts = currentTime;
			}
		}
	}

	// 2. Reconnection logic when connection is lost
	if (reconnectts > 0 && !wificonnected && !connecting) {
		if (currentTime - reconnectts >= reconnectDelay) {
			Serial.println("[WiFi] Attempting to reconnect...");
			connect(wifissid, wifipassword);
			reconnectts = 0;
		}
	}

	// 3. Optional periodic reconnect every 10 minutes
	if (periodicReconnectEnabled && wificonnected) {
		if (lastPeriodicReconnect == 0) {
			lastPeriodicReconnect = currentTime; // Initialize on first connection
		} else if (currentTime - lastPeriodicReconnect >= periodicReconnectInterval) {
			Serial.println("[WiFi] Triggering periodic reconnect (10-minute interval)");
			lastPeriodicReconnect = currentTime;
			// Disconnect and reconnect
			WiFi.disconnect(false, false); // Don't erase config
			reconnectts = currentTime; // Trigger reconnection after delay
			wificonnected = false;
			connecting = false;
		}
	}

	return wificonnected;
}
void onConnected();
////////////////////
const char* wifiDisconnectReason(uint8_t reason);
const char* wifiStatusString(wl_status_t status);

void connectWifi(String ssid, String password) {
	//WiFi.onEvent(wiFiEvent);
 	WiFiEventId_t eventID = WiFi.onEvent(
			[](WiFiEvent_t event, WiFiEventInfo_t info) {
		reconnectts=millis();
		Serial.print("[WiFi] Lost connection. Reason: ");
		Serial.println(info.wifi_sta_disconnected.reason);
		Serial.print(wifiDisconnectReason(info.wifi_sta_disconnected.reason));
		connectionfailed=true;
		wificonnected=false;
		connecting=false;
		Serial.println(String()+" WiFi status:"+WiFi.status()+" "+wifiStatusString(WiFi.status()));
	//	delay(3000);
	//	Serial.println(String()+" WiFi status 3sec later :"+WiFi.status()+" "+wifiStatusString(WiFi.status()));
	},
	WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_DISCONNECTED
	);

    WiFi.onEvent([](WiFiEvent_t event, WiFiEventInfo_t info) {
        Serial.println(String() + "Connected to the WiFi network at " + millis() + "ms (Waiting for DHCP...)");
    }, WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_CONNECTED);

    WiFiEventId_t eventID2 = WiFi.onEvent(
			[](WiFiEvent_t event, WiFiEventInfo_t info) {
		Serial.println(String()+"Connected with IP to the WiFi network in "+millis()+"ms");
		Serial.print("Local ESP32 IP: ");
		Serial.println(WiFi.localIP());

		wificonnected=true;
		connecting=false;
		connectionfailed=false;
		lastPeriodicReconnect = millis(); // Initialize periodic reconnect timer
		onConnected();
	},
	WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_GOT_IP
	);

	// Initialize timing variables
	lastConnectionCheck = millis();
	lastPeriodicReconnect = 0;

	connect(ssid,password);

}


void onConnected(){

}

// Function to enable/disable periodic reconnect (10-minute intervals)
void setPeriodicReconnect(bool enabled) {
	periodicReconnectEnabled = enabled;
	Serial.printf("[WiFi] Periodic reconnect %s\n", enabled ? "enabled" : "disabled");
}

// Function to set custom periodic reconnect interval (in milliseconds)
void setPeriodicReconnectInterval(unsigned long intervalMs) {
	periodicReconnectInterval = intervalMs;
	Serial.printf("[WiFi] Periodic reconnect interval set to %lu ms\n", intervalMs);
}

// Function to set custom connection check interval (in milliseconds)
void setConnectionCheckInterval(unsigned long intervalMs) {
	connectionCheckInterval = intervalMs;
	Serial.printf("[WiFi] Connection check interval set to %lu ms\n", intervalMs);
}

// Function to set custom reconnection delay (in milliseconds)
void setReconnectDelay(unsigned long delayMs) {
	reconnectDelay = delayMs;
	Serial.printf("[WiFi] Reconnect delay set to %lu ms\n", delayMs);
}

const char* wifiStatusString(wl_status_t status) {
  switch (status) {
    case WL_NO_SHIELD:        return "No WiFi hardware";
    case WL_IDLE_STATUS:      return "Idle";
    case WL_NO_SSID_AVAIL:    return "SSID not available";
    case WL_SCAN_COMPLETED:   return "Scan completed";
    case WL_CONNECTED:        return "Connected";
    case WL_CONNECT_FAILED:   return "Connection failed";
    case WL_CONNECTION_LOST:  return "Connection lost";
    case WL_DISCONNECTED:     return "Disconnected";
    default:                  return "Unknown status";
  }
}

const char* wifiDisconnectReason(uint8_t reason) {
  switch (reason) {
    case WIFI_REASON_UNSPECIFIED:              return "Unspecified";
    case WIFI_REASON_AUTH_EXPIRE:              return "Authentication expired";
    case WIFI_REASON_AUTH_LEAVE:               return "Authentication leave";
    case WIFI_REASON_ASSOC_EXPIRE:             return "Association expired";
    case WIFI_REASON_ASSOC_TOOMANY:            return "Too many associations";
    case WIFI_REASON_NOT_AUTHED:               return "Not authenticated";
    case WIFI_REASON_NOT_ASSOCED:              return "Not associated";
    case WIFI_REASON_ASSOC_LEAVE:              return "Association leave";
    case WIFI_REASON_ASSOC_NOT_AUTHED:         return "Association but not authenticated";
    case WIFI_REASON_DISASSOC_PWRCAP_BAD:      return "Disassociated: power capability unacceptable";
    case WIFI_REASON_DISASSOC_SUPCHAN_BAD:     return "Disassociated: supported channel unacceptable";
    case WIFI_REASON_IE_INVALID:               return "Invalid information element";
    case WIFI_REASON_MIC_FAILURE:              return "MIC failure";
    case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:   return "4-way handshake timeout";
    case WIFI_REASON_GROUP_KEY_UPDATE_TIMEOUT: return "Group key update timeout";
    case WIFI_REASON_IE_IN_4WAY_DIFFERS:       return "Information element differs in 4-way handshake";
    case WIFI_REASON_GROUP_CIPHER_INVALID:     return "Invalid group cipher";
    case WIFI_REASON_PAIRWISE_CIPHER_INVALID:  return "Invalid pairwise cipher";
    case WIFI_REASON_AKMP_INVALID:             return "Invalid AKMP";
    case WIFI_REASON_UNSUPP_RSN_IE_VERSION:    return "Unsupported RSN IE version";
    case WIFI_REASON_INVALID_RSN_IE_CAP:       return "Invalid RSN IE capabilities";
    case WIFI_REASON_802_1X_AUTH_FAILED:       return "802.1X authentication failed";
    case WIFI_REASON_CIPHER_SUITE_REJECTED:    return "Cipher suite rejected";
    case WIFI_REASON_BEACON_TIMEOUT:           return "Beacon timeout (AP not responding)";
    case WIFI_REASON_NO_AP_FOUND:              return "Access point not found";
    case WIFI_REASON_AUTH_FAIL:                return "Authentication failed (wrong password?)";
    case WIFI_REASON_ASSOC_FAIL:               return "Association failed";
    case WIFI_REASON_HANDSHAKE_TIMEOUT:        return "Handshake timeout";
    case WIFI_REASON_CONNECTION_FAIL:          return "Connection failed";
    default:                                   return "Unknown reason";
  }
}
#endif
