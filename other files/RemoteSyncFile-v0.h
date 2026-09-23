#ifndef REMOTESYNCFILE_H
#define REMOTESYNCFILE_H

#include <functional> // Add this near the top with <map>
#include "AsyncHttpRequest.h"

String loadFileToString(String filename);

// --- NEW BASE CLASS FOR SYNCHRONIZATION ---
class RemoteSyncFile {
protected:
	String syncUrl;
	String syncFilename;
	String versionFilename;
	String localDBVersion = "";

	// Pure virtual function forced on child to handle memory refresh
	virtual void onFileUpdated() = 0;

public:
	// UI Callbacks (So DataManager can blink LEDs or Beep)
	std::function<void()> onUpdateSuccess;
	std::function<void()> onUpdateError;
	std::function<void()> onUpdateBusy;
	std::function<void()> onUpToDate;

	void initSync(String url, String filename) {
		syncUrl = url;
		syncFilename = filename;
		versionFilename = filename + ".version";

		localDBVersion = loadFileToString(versionFilename);
		localDBVersion.trim();
		if (localDBVersion.length() == 0) {
			localDBVersion = "0"; // Default to version 0 if no file exists
		}
		Serial.println(String()+"Local DB Version for "+filename+": " + localDBVersion);
	}

	void syncServerDatabase() {
		if (syncUrl.length() == 0) return;
		String requrl = syncUrl + "db_version";
		Serial.println("Checking remote version at: " + requrl);

		AsyncHttpRequest::fetch(
			requrl,
			[this](const StreamString *content, AsyncHttpRequest *req) {
				String remoteVersion = content->c_str();
				remoteVersion.trim();
				Serial.println("Remote DB Version: " + remoteVersion);

				if (remoteVersion.length() > 0 && remoteVersion != localDBVersion) {
					Serial.println("Version mismatch! Downloading new file...");
					downloadServerDatabase(remoteVersion);
				} else {
					Serial.println("Local file is up to date.");
					if (onUpToDate) onUpToDate();
				}
			},
			[this](const String &error, AsyncHttpRequest *req) {
				Serial.println("Failed to check DB version. " + error);
				if (onUpdateError) onUpdateError();
			}
		);
	}

	void downloadServerDatabase(String newVersion) {
		String requrl = syncUrl + "db_download";
		if (onUpdateBusy) onUpdateBusy();

		AsyncHttpRequest::fetch(
			requrl,
			[this, newVersion](const StreamString *content, AsyncHttpRequest *req) {
				String payload = content->c_str();

				// Basic sanity check for JSON formatting
				if(payload.startsWith("{") && payload.endsWith("}")) {
					Serial.println("Download successful. Saving to disk...");

					// Save new database and new version files
					SimpleFS.rewriteFile(String("/") + syncFilename, payload);
					SimpleFS.rewriteFile(String("/") + versionFilename, newVersion);
					localDBVersion = newVersion;

					onFileUpdated(); // Tell the child class to reload into memory

					if (onUpdateSuccess) onUpdateSuccess();
				} else {
					Serial.println("Downloaded DB is not valid JSON!");
					if (onUpdateError) onUpdateError();
				}
			},
			[this](const String &error, AsyncHttpRequest *req) {
				Serial.println("Failed to download database. " + error);
				if (onUpdateError) onUpdateError();
			}
		);
	}
};

#endif
