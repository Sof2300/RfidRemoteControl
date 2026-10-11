#ifndef CONFIGMODE_H
#define CONFIGMODE_H

#include <Arduino.h>
#include <Ticker.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <ArduinoJson.h>

//#include "FileUtils.h"
#include <FS/SimpleFS.H>
#include "ResetDetector.H"

#define CONFIG_MODE_PAGE "edit.html"

// ==============================================================================
// WEB SERVER HTML CONFIGURATION
// ==============================================================================


#ifndef USE_CUSTOM_HTML_ONLY
const char* setupPageHTML = R"HTML(
<!DOCTYPE html>
<html>
<head>
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <title>ESP32 Setup</title>
    <style>
        body { font-family: Arial, sans-serif; margin: 20px; background: #f4f4f4; }
        .container { background: #fff; padding: 20px; border-radius: 8px; max-width: 400px; margin: auto; box-shadow: 0 0 10px rgba(0,0,0,0.1); }
        .form-group { margin-bottom: 15px; }
        label { display: block; margin-bottom: 5px; font-weight: bold; }
        input[type="text"], input[type="password"] { width: 100%; padding: 8px; box-sizing: border-box; border: 1px solid #ccc; border-radius: 4px; }
        button { width: 100%; padding: 10px; background: #28a745; color: white; border: none; border-radius: 4px; cursor: pointer; font-size: 16px; }
        button:hover { background: #218838; }
    </style>
</head>
<body>
    <div class="container">
        <h2>System Setup</h2>
        <form action="/save" method="POST">
            <div class="form-group">
                <label for="ssid">WiFi SSID:</label>
                <input type="text" id="ssid" name="ssid" placeholder="Enter network name" required>
            </div>

            <div class="form-group">
                <label for="password">WiFi Password:</label>
                <input type="password" id="password" name="password" placeholder="Enter password">
            </div>

            <div class="form-group">
                <label for="superscale">Superscale:</label>
                <input type="text" id="superscale" name="superscale" placeholder="Enter superscale IP">
            </div>
            <!-- Future fields (mDNS, Mode AP/STA) will go here as new .form-group divs -->
            <button type="submit">Save Configuration</button>
        </form>
    </div>
</body>
</html>
)HTML";
#else
const char* setupPageHTML = "<h1>Please place edit.html or edit.html.gz on the filesystem</h1>";
#endif
// ==============================================================================
// CONFIG MODE MANAGER
// ==============================================================================
class ConfigModeManager {
private:
	IPAddress apIP;
	DNSServer dnsServer;
	WebServer server;
	bool inConfigMode;
	ResetDetector resetDetector;
	String configfilename;
	String configmodepage;

public:
	ConfigModeManager(String configfilename0, String configmodepage0=CONFIG_MODE_PAGE) :
		apIP(192, 168, 4, 1), server(80), inConfigMode(false), resetDetector(2000),
		configfilename(configfilename0), configmodepage(configmodepage0) {}

	bool isInConfigMode() const { return inConfigMode; }

	bool streamFileByServer(String filename, String contentType="text/html"){
		bool fileFound = false;
		if(SIMPLEFS.exists(filename+".gz")) {filename+=".gz";}
		FileFS* f = SIMPLEFS.openFile(filename.c_str(), "r");
		if (f) {
			SimpleFileFS *filefs=(SimpleFileFS*) f;
			server.streamFile(filefs->fileptr, contentType.c_str());
			SIMPLEFS.closeFile(f);
			fileFound = true;
			Serial.println(String()+"Serving "+filename+" from filesystem");
		}
		return fileFound;

	}


	void begin() {
		// Initialize reset detector
		resetDetector.begin();
		uint32_t resets = resetDetector.getCount();
		Serial.printf("\nBoot resets so far: %u\n", resets);

		// If user hit reset 3 times, launch AP and WebServer
		if (resets >= 3) {
			Serial.println("3 Resets detected! Entering Configuration Mode...");
			inConfigMode = true;

			// 1. Setup AP Mode
			WiFi.mode(WIFI_AP);
			WiFi.softAP("ESP32-Setup");
			Serial.print("AP Started. IP Address: ");
			Serial.println(WiFi.softAPIP());

			dnsServer.start(53, "*", apIP);
			SIMPLEFS.begin();
			// 2. Define Web Routes
			server.on("/", HTTP_GET, [this]() {
				// Check for edit.html or edit.html.gz on filesystem first
				String filename=String()+"/"+configmodepage;
				bool fileFound=streamFileByServer(filename);
				// If no custom file found, serve built-in HTML
				if (!fileFound) {
					server.send(200, "text/html", setupPageHTML);
					Serial.println("Serving built-in setup page");
				}
			});

			// Generic file serving - serve any file at its path using streaming
			server.onNotFound([this]() {
				String filePath = server.uri();
				Serial.println(String()+"Request for: "+filePath);

				String contentType = "text/plain";// Determine content type
				if(filePath.endsWith(".html")) contentType = "text/html";
				else if(filePath.endsWith(".css")) contentType = "text/css";
				else if(filePath.endsWith(".js")) contentType = "application/javascript";
				else if(filePath.endsWith(".json")) contentType = "application/json";
				else if(filePath.endsWith(".gz")) contentType = "application/gzip";

				bool fileFound=streamFileByServer(filePath, contentType);
				// Check if file exists on filesystem (prefer .gz version)
				if(!fileFound) {
					server.send(404, "text/plain", "File not found: " + filePath);
					Serial.println(String()+"File not found: "+filePath);
					return;
				}


			});

			server.on("/save", HTTP_POST, [this]() {
				// Extract the fields submitted by the form
				String ssid = server.arg("ssid");
				String password = server.arg("password");
				String superscale = server.arg("superscale");

				Serial.println("\n--- Received New Config ---");
				Serial.println("SSID: " + ssid);
				Serial.println("Password: " + password);
				Serial.println("Superscale: " + superscale);

				// Save to config.json
				String filecontent = loadFileToString(configfilename);
				DynamicJsonDocument doc = parseJSON(filecontent);
				JsonObject root = doc.as<JsonObject>();

				if (ssid.length() > 0) root[SSID_KEY] = ssid;
				if (password.length() > 0) root[PASSWORD_KEY] = password;
				if (superscale.length() > 0) root[SUPERSCALE_URL_FIELD] = superscale;

				String output;
				serializeJson(root, output);

				// Write to file using SIMPLEFS pattern
				FileFS* f = SIMPLEFS.openFile((String("/")+configfilename).c_str(), "w");
				if (f) {
					SimpleFileFS* sf = (SimpleFileFS*)f;
					sf->fileptr.print(output);
					SIMPLEFS.closeFile(f);
					Serial.println("Config saved to file");
					// Send a success message back to the browser
					server.send(200, "text/html", "<h3>Configuration Saved!</h3><p>Check Serial Monitor. You can restart the device now.</p>");

				} else {
					Serial.println("Failed to open config file for writing");
					// Send a success message back to the browser
					server.send(200, "text/html", "<h3>Configuration not saved !</h3><p>Failed to open config file for writing</p>");

				}
			});

			// File write endpoint for edit.html
			server.on("/filewrite", HTTP_POST, [this]() {
				String path = server.arg("path");
				String content = server.arg("content");

				Serial.println(String()+"Filewrite request: path="+path+" content length="+content.length());

				// Remove http:// and host if present
				String p0 = path;
				if(p0.startsWith("http://")) {
					int pos = p0.indexOf("/", 7);
					if(pos >= 0) p0 = p0.substring(pos);
				}

				// Write file using SimpleFS.rewriteFile
				bool success = SIMPLEFS.rewriteFile(p0.c_str(), (unsigned char *)content.c_str(), content.length());

				if(success) {
					server.send(200, "text/plain", "File updated " + p0);
					Serial.println(String()+"File written successfully: "+p0);
					SIMPLEFS.printFileBis(path.c_str());
				} else {
					server.send(500, "text/plain", "File NOT updated " + p0);
					Serial.println(String()+"Failed to write file: "+p0);
				}
			});

			// 3. Start Web Server
			server.begin();
			Serial.println("Web server listening!");
			delay(2000);// this allows that if you do a 4th, you'll not reboot in normal mode
			// Clear the reset counter so normal boot happens next time
			resetDetector.resetCounter();
		} else {
			Serial.println("Normal Boot Mode.");
		}
	}

	void handleLoop() {
		if (inConfigMode) {
			dnsServer.processNextRequest();
			server.handleClient();
		}
	}
};

#endif
