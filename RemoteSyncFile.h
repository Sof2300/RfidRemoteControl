#ifndef REMOTESYNCFILE_H
#define REMOTESYNCFILE_H

#include <functional>
#include "Definitions.h"
#include "AsyncHttpRequest.h"
#include "FileUtils.h"


#ifndef SYNC_DEBUG_LEVEL
#define SYNC_DEBUG_LEVEL 1
#endif

#if SYNC_DEBUG_LEVEL >= 1
#define SYNC_LOG1(...) Serial.printf(__VA_ARGS__)
#else
#define SYNC_LOG1(...)
#endif

#if SYNC_DEBUG_LEVEL >= 2
#define SYNC_LOG2(...) Serial.printf(__VA_ARGS__)
#else
#define SYNC_LOG2(...)
#endif


#define SYNC_TIMEOUT 5000


class RemoteSyncFile {
protected:
	String syncUrl;
	String syncFilename;
	size_t lastSyncedSize = 0;

	// Virtual function called when the file is updated (Full or Delta)
	virtual void onFileUpdated() = 0;

	void notifyFileUpdated(const char *reason) {
		size_t sz = getLocalSize();
		SYNC_LOG2("[SYNC] onFileUpdated triggered (%s). newSize=%u CRC32=%s lastSyncedSize=%u\n",
				reason,
				(unsigned)sz,
				getLocalCRC(sz).c_str(),
				(unsigned)lastSyncedSize);
		onFileUpdated();
	}

	bool reachableHttpCode(int httpCode){
		if (httpCode <= 0) {
			SYNC_LOG1("ERROR: Server is NOT REACHABLE!\n");
			SYNC_LOG1("Reason: %s\n", this->lastErrorMsg);
			return false;
			// lastErrorMsg will be things like: "connection refused", "timeout", or "DNS failed"
		}
		// Otherwise, the server was reached, but it returned an HTTP error
		else {
			SYNC_LOG1("ERROR: Server reached, but returned HTTP Code: %d\n", this->lastHttpCode);

			if (httpCode == 404) {
				SYNC_LOG1("Reason: File or endpoint not found on server.\n");
			} else if (httpCode >= 500) {
				SYNC_LOG1("Reason: Server crashed or had an internal error.\n");
			}

			return true;
		}
	}
public:
	// UI Callbacks
	std::function<void()> onUpdateSuccess;
	std::function<void()> onUpdateError;
	std::function<void()> onUpdateBusy;
	std::function<void()> onUpToDate;
	std::function<void()> onDuplicateIdError;

	// ERROR TRACKING VARIABLES ---
	String lastErrorMsg = "";
	int lastHttpCode = 0;

	size_t getLocalSize() {
		return SIMPLEFS.fileSize(syncFilename.c_str());
	}
	String getLocalCRC(size_t limit) {return getFileCRC(syncFilename.c_str(),limit);}

	void initSync(String url, String filename) {
		syncUrl = url;
		syncFilename=filename;
		size_t sz = getLocalSize();
		lastSyncedSize = sz;

		SYNC_LOG2("[SYNC] initSync('%s') url=%s file=%s\n",
				filename.c_str(), syncUrl.c_str(), syncFilename.c_str());
		SYNC_LOG2("[SYNC] Local DB: Size=%u CRC32=%s lastSyncedSize=%u\n",
				(unsigned)sz,
				getLocalCRC(sz).c_str(),
				(unsigned)lastSyncedSize);
	}

	// --- 1. THE VERSION CHECK ---
	void syncServerDatabase() {
		SYNC_LOG1("[SYNC] ==== syncServerDatabase START ====\n");
		if (syncUrl.length() == 0) {
			SYNC_LOG1("[SYNC] syncServerDatabase: syncUrl empty -> ABORT\n");
			return;
		}

		size_t currentSize = getLocalSize();
		size_t checkSize = (lastSyncedSize > 0 && lastSyncedSize <= currentSize) ? lastSyncedSize : currentSize;
		String crc = getLocalCRC(checkSize);

		SYNC_LOG2("[SYNC] local state -> currentSize=%u lastSyncedSize=%u checkSize=%u CRC@checkSize=%s\n",
				(unsigned)currentSize, (unsigned)lastSyncedSize, (unsigned)checkSize, crc.c_str());

		String reqUrl = syncUrl + "sync_check?size=" + String(checkSize)
																	+ "&crc=" + crc
																	+ "&total_size=" + String(currentSize);

		SYNC_LOG2("[SYNC] GET -> %s\n", reqUrl.c_str());

		AsyncHttpRequest::fetch(
				reqUrl,
				[this, currentSize, checkSize, crc](const StreamString *content, AsyncHttpRequest *req) {
			String response = content->c_str();
			response.trim();
			SYNC_LOG2("[SYNC] server sync_check response: '%s'\n", response.c_str());

			if (response == "UP_TO_DATE") {
				SYNC_LOG2("[SYNC] -> UP_TO_DATE. updating lastSyncedSize %u -> %u\n",
						(unsigned)lastSyncedSize, (unsigned)currentSize);
				lastSyncedSize = currentSize;
				SYNC_LOG1("[SYNC] ==== syncServerDatabase END (UP_TO_DATE) ====\n");
				if (onUpToDate) onUpToDate();
				// Also notify generic success handlers so UI using onUpdateSuccess
				// gets informed when a sync finishes with no changes.
				if (onUpdateSuccess) {
					SYNC_LOG2("[SYNC] syncServerDatabase: firing onUpdateSuccess UI callback for UP_TO_DATE\n");
					onUpdateSuccess();
				}
			}
			else if (response == "FULL_UPDATE") {
				// CRC mismatch -> Client must download full file
				SYNC_LOG1("[SYNC] -> FULL_UPDATE (CRC mismatch), triggering full download\n");
				SYNC_LOG1("[SYNC] ==== syncServerDatabase END (->downloadFull) ====\n");
				downloadFull();
			}
			else if (response == "SERVER_BUSY") {
				// Server is busy, error out
				SYNC_LOG1("[SYNC] -> SERVER_BUSY, cannot sync\n");
				this->lastErrorMsg = "SERVER_BUSY";
				this->lastHttpCode = req ? req->responseHTTPcode() : 0;
				SYNC_LOG1("[SYNC] ==== syncServerDatabase END (SERVER_BUSY) ====\n");
				if (onUpdateError) onUpdateError();
			}
			else {
				// Other error responses (missing parameters, not initialized, etc.)
				SYNC_LOG1("[SYNC] -> Error response: '%s', cannot sync\n", response.c_str());
				this->lastErrorMsg = response;
				this->lastHttpCode = req ? req->responseHTTPcode() : 0;
				SYNC_LOG1("[SYNC] ==== syncServerDatabase END (ERROR) ====\n");
				if (onUpdateError) onUpdateError();
			}
		},
		[this](const String &error, AsyncHttpRequest *req) {
			SYNC_LOG1("[SYNC] syncServerDatabase: request FAILED. %s\n", error.c_str());
			this->lastErrorMsg = error;
			this->lastHttpCode = req ? req->responseHTTPcode() : 0;
			SYNC_LOG1("[SYNC] ==== syncServerDatabase END (NETWORK ERROR) ====\n");
			if (onUpdateError) onUpdateError();
		}, SYNC_CHANNEL_NAME
		,SYNC_TIMEOUT);
	}



	void appendRecord(String key, String value) {
		size_t beforeSize = getLocalSize();
		SYNC_LOG2("[SYNC] appendRecord('%s','%s'). file BEFORE size=%u lastSyncedSize=%u (deltaNotYetPushed=%u)\n",
				key.c_str(), value.c_str(),
				(unsigned)beforeSize, (unsigned)lastSyncedSize,
				beforeSize > lastSyncedSize ? (unsigned)(beforeSize - lastSyncedSize) : 0u);

		String line = String(key) + "," + value + "\n";
		SIMPLEFS.appendToFile(syncFilename, line);
		SYNC_LOG2("File %s after append: size:%u content:\n'%s'\n", syncFilename.c_str(), SIMPLEFS.fileSize(syncFilename.c_str()), SIMPLEFS.readFileToString(syncFilename.c_str()).c_str());

		size_t afterSize = getLocalSize();
		SYNC_LOG2("[SYNC] appendRecord: OK. size %u -> %u (+%u bytes). lastSyncedSize REMAINING at %u (will push on next sync)\n",
				(unsigned)beforeSize, (unsigned)afterSize, (unsigned)(afterSize - beforeSize),
				(unsigned)lastSyncedSize);
	}

	bool removeRecordByKey(String key) {
		size_t beforeSize = getLocalSize();
		SYNC_LOG2("[SYNC] removeRecordByKey('%s'). BEFORE size=%u lastSyncedSize=%u\n",
				key.c_str(), (unsigned)beforeSize, (unsigned)lastSyncedSize);
		if (lastSyncedSize > beforeSize) {
			SYNC_LOG1("[SYNC] WARNING: lastSyncedSize=%u > beforeSize=%u -> will be reset to new shrunk size after removal\n",
					(unsigned)lastSyncedSize, (unsigned)beforeSize);
		}

		FileFS* src = SIMPLEFS.openFile(syncFilename.c_str(), "r");
		if (!src) {
			SYNC_LOG1("[SYNC] removeRecordByKey: FAIL cannot open source CSV.\n");
			return false;
		}

		String tempName = syncFilename + ".tmp";
		FileFS* dst = SIMPLEFS.openFile(tempName.c_str(), "w");
		if (!dst) {
			SIMPLEFS.closeFile(src);
			SYNC_LOG1("[SYNC] removeRecordByKey: FAIL cannot open tmp for write.\n");
			return false;
		}

		const size_t BLOCK_SIZE = 1024;
		char buffer[BLOCK_SIZE + 1];
		String carryOver = "";
		String searchTarget = key + ",";
		bool removed = false;
		unsigned linesKept = 0, linesDropped = 0;

		auto writeLine = [&](const String &line) {
			if (line.length() == 0) return;
			if (line.startsWith(searchTarget)) {
				removed = true;
				linesDropped++;
				int commaIdx = line.indexOf(',');
				String removedValue = commaIdx != -1 ? line.substring(commaIdx + 1) : String("");
				SYNC_LOG2("[SYNC] removeRecordByKey: DROPPED line -> key='%s' value='%s'\n",
						key.c_str(), removedValue.c_str());
				return;
			}
			linesKept++;
			SimpleFileFS* sdst = (SimpleFileFS*)dst;
			sdst->fileptr.print(line);
			sdst->fileptr.print('\n');
		};

		while (true) {
			size_t bytesRead = SIMPLEFS.readFile((unsigned char*)buffer, BLOCK_SIZE, src);
			if (bytesRead == 0) break;
			buffer[bytesRead] = '\0';
			String chunk = carryOver + String(buffer);

			int lastNewline = chunk.lastIndexOf('\n');
			if (lastNewline != -1) {
				String processPart = chunk.substring(0, lastNewline);
				carryOver = chunk.substring(lastNewline + 1);

				int lineStart = 0;
				while (lineStart < processPart.length()) {
					int lineEnd = processPart.indexOf('\n', lineStart);
					if (lineEnd == -1) lineEnd = processPart.length();
					writeLine(processPart.substring(lineStart, lineEnd));
					lineStart = lineEnd + 1;
				}
			} else {
				carryOver = chunk;
			}
		}

		if (carryOver.length() > 0) writeLine(carryOver);

		SIMPLEFS.closeFile(src);
		SIMPLEFS.closeFile(dst);

		if (!removed) {
			SIMPLEFS.erase(tempName);
			SYNC_LOG1("[SYNC] removeRecordByKey: key '%s' NOT FOUND. file unchanged. size=%u lines inspected=%u\n",
					key.c_str(), (unsigned)beforeSize, linesKept);
			return false;
		}

		SIMPLEFS.erase(syncFilename.c_str());
		SIMPLEFS.rename(tempName, syncFilename);

		size_t afterSize = getLocalSize();
		if (lastSyncedSize > afterSize) {
			SYNC_LOG1("[SYNC] removeRecordByKey: physical delete SHORTENED file below lastSyncedSize. resetting lastSyncedSize %u -> %u (next sync may trigger FULL_UPDATE)\n",
					(unsigned)lastSyncedSize, (unsigned)afterSize);
			lastSyncedSize = afterSize;
		}
		SYNC_LOG2("[SYNC] removeRecordByKey: OK. lines kept=%u dropped=%u. size %u -> %u. newCRC=%s lastSyncedSize=%u\n",
				linesKept, linesDropped,
				(unsigned)beforeSize, (unsigned)afterSize,
				getLocalCRC(afterSize).c_str(),
				(unsigned)lastSyncedSize);
		return true;
	}



protected:
	void submitModification(size_t start, size_t end, String content) {
		size_t currentSize = getLocalSize();
		// If the new content is shorter than the old block, pad it with spaces.
		if (end > start && content.length() < (end - start)) {
			size_t targetLen = end - start;
			// Temporarily strip newline if it exists
			if (content.endsWith("\n")) content = content.substring(0, content.length() - 1);
			// Pad with spaces until we are exactly 1 byte short of target
			while (content.length() < targetLen - 1) content += " ";
			// End with newline so CSV parsing for the *next* line doesn't break
			content += "\n";
			SYNC_LOG2("[SYNC] Padded record to %u bytes to prevent flash shift.\n", (unsigned)targetLen);
		}
		String currentCrc = getLocalCRC(currentSize);

		if (onUpdateBusy) onUpdateBusy();

		// Replace "sync_patch" with your actual backend endpoint if needed
		String reqUrl = syncUrl + "sync_patch";

		// Generate headers using the helper function
		auto headers = buildPatchHeaders(currentCrc, start, end, currentSize, content.length());
		SYNC_LOG1("[SYNC] submitModification PATCH -> url: %s (range: %u-%u/%u, contentLen: %u, CRC: %s)\n",
				reqUrl.c_str(), (unsigned)start, (unsigned)end, (unsigned)currentSize, (unsigned)content.length(), currentCrc.c_str());

		SYNC_LOG2("[SYNC] submitModification PATCH -> %s (If-Match: %s, Range: %u-%u/%u)\n",
				reqUrl.c_str(), currentCrc.c_str(), (unsigned)start, (unsigned)end, (unsigned)currentSize);

		AsyncHttpRequest::request("PATCH", reqUrl, content, headers,
				[this, start, end, content](const StreamString *response, AsyncHttpRequest *req) {
			Serial.println("[SYNC] PATCH callback ENTER");
			String responseStr = response->c_str();
			responseStr.trim();
			SYNC_LOG2("[SYNC] Server PATCH response: '%s'\n", responseStr.c_str());

			if (responseStr != "OK") {
				SYNC_LOG1("[SYNC] Server rejected PATCH with response: '%s'\n", responseStr.c_str());
				this->lastErrorMsg = responseStr;
				this->lastHttpCode = req ? req->responseHTTPcode() : 0;
				if (onUpdateError) onUpdateError();
				Serial.println("[SYNC] PATCH callback EXIT (ERROR)");
				return;
			}

			SYNC_LOG1("[SYNC] Server accepted PATCH. Applying locally (range: %u-%u, contentLen: %u)...\n",
					(unsigned)start, (unsigned)end, (unsigned)content.length());
			String oldCrc=getFileCRC(syncFilename);
			Serial.println("[SYNC] Before spliceFileInPlace");
			bool success = spliceFileInPlace(syncFilename, start, end, content);
			Serial.println("[SYNC] After spliceFileInPlace");
			if (success) {
				lastSyncedSize=SIMPLEFS.fileSize(syncFilename.c_str());
				SYNC_LOG1("[SYNC] Local splice OK. New file size: %u, CRC: %s\n",
						(unsigned)lastSyncedSize, getLocalCRC(lastSyncedSize).c_str());
				notifyFileUpdated("submitModification");
				Serial.println("[SYNC] Before onUpdateSuccess");
				if (onUpdateSuccess) onUpdateSuccess();
				Serial.println("[SYNC] After onUpdateSuccess");
			} else {
				SYNC_LOG1("[SYNC] ERROR: Server accepted, but local write failed!\n");
				if (onUpdateError) onUpdateError();
			}
			Serial.println("[SYNC] PATCH callback EXIT");
		},
		[this](const String &error, AsyncHttpRequest *req) {
			this->lastErrorMsg = error;
			this->lastHttpCode = req ? req->responseHTTPcode() : 0;
			SYNC_LOG1("[SYNC] PATCH Failed (HTTP %d): %s\n", this->lastHttpCode, error.c_str());

			// 412 = Precondition Failed (Someone else changed the file first)
			if (this->lastHttpCode == 412) {
				if (error.indexOf("SIZE MISMATCH") != -1) {
					SYNC_LOG1("[SYNC] SIZE MISMATCH! Server file size changed. Dispatching error for recovery...\n");
					SYNC_LOG1("[SYNC] Details: %s\n", error.c_str());
				}
				else if (error.indexOf("CRC MISMATCH") != -1) {
					SYNC_LOG1("[SYNC] CRC MISMATCH! Server file content changed. Dispatching error for recovery...\n");
					SYNC_LOG1("[SYNC] Details: %s\n", error.c_str());
				}
				else {
					SYNC_LOG1("[SYNC] Precondition Failed (412)! Server file changed. Dispatching error for recovery...\n");
					SYNC_LOG1("[SYNC] Details: %s\n", error.c_str());
				}
				// Dispatch error event so state machine can start recovery
				if (onUpdateError) onUpdateError();
			}
			// Check for duplicate ID error from server
			else if (error.indexOf("DUPLICATE_ID") != -1) {
				SYNC_LOG1("[SYNC] Duplicate ID conflict detected by server. Rejecting operation...\n");
				if (onDuplicateIdError) onDuplicateIdError();
			}
			// Check for SERVER_BUSY
			else if (error.indexOf("SERVER_BUSY") != -1) {
				SYNC_LOG1("[SYNC] Server is busy. Cannot apply patch.\n");
				if (onUpdateError) onUpdateError();
			}
			// Check for FAILED response (splice operation failed on server)
			else if (error.indexOf("FAILED") != -1) {
				SYNC_LOG1("[SYNC] Server splice operation failed. Error: %s\n", error.c_str());
				if (onUpdateError) onUpdateError();
			}
			else {
				// Dispatch error event for other errors
				SYNC_LOG1("[SYNC] Generic PATCH error: %s\n", error.c_str());
				if (onUpdateError) onUpdateError();
			}
		}, "", SYNC_TIMEOUT
		);
	}

	// --- FULL OVERWRITE ---
	void downloadFull(/*int newMod, size_t newOffset*/) {
		size_t beforeSize = getLocalSize();
		String reqUrl = syncUrl+ "sync_download";
		//String reqUrl = syncUrl+ syncFilename;
		SYNC_LOG2("[SYNC] downloadFull START. beforeSize=%u beforeCRC=%s GET -> %s\n",
				(unsigned)beforeSize, getLocalCRC(beforeSize).c_str(), reqUrl.c_str());

		if (onUpdateBusy) onUpdateBusy();

		FileFS* f = SIMPLEFS.openFile(syncFilename.c_str(), "w");
		if (!f) {
			SYNC_LOG1("[SYNC] downloadFull: FAIL cannot open file for write\n");
			if (onUpdateError) onUpdateError();
			return;
		}
		SimpleFileFS* sf = (SimpleFileFS*)f;
		AsyncHttpRequest::fetchToFile(
				reqUrl,
				sf->fileptr,
				[this, beforeSize, f](const StreamString *content, AsyncHttpRequest *req) {
			// File is closed automatically by AsyncHttpRequest when done
			// No need to call SIMPLEFS.closeFile(f) here
			if (content) {
				SYNC_LOG2("[SYNC] downloadFull CALLBACK content length=%u\n", (unsigned)content->length());
				SYNC_LOG2("[SYNC] downloadFull CALLBACK content=\"%s\"\n", content->c_str());
			} else {
				SYNC_LOG2("[SYNC] downloadFull CALLBACK content=null (streamed to file)\n");
			}
			size_t afterSize = getLocalSize();
			lastSyncedSize = afterSize;
			SYNC_LOG1("[SYNC] downloadFull SUCCESS. beforeSize=%u afterSize=%u (+%u bytes) afterCRC=%s lastSyncedSize=%u\n",
					(unsigned)beforeSize, (unsigned)afterSize, (unsigned)(afterSize - beforeSize),
					getLocalCRC(afterSize).c_str(),
					(unsigned)lastSyncedSize);
			notifyFileUpdated("downloadFull");
			if (onUpdateSuccess) {
				SYNC_LOG2("[SYNC] downloadFull: firing onUpdateSuccess UI callback\n");
				onUpdateSuccess();
			}
		},
		[this, beforeSize, f](const String &error, AsyncHttpRequest *req) {
			// File is closed automatically by AsyncHttpRequest when done
			SYNC_LOG1("[SYNC] downloadFull FAILED. beforeSize=%u error=%s\n",
					(unsigned)beforeSize, error.c_str());
			this->lastErrorMsg = error;
			this->lastHttpCode = req ? req->responseHTTPcode() : 0;
			SYNC_LOG2("[SYNC] downloadFull: firing onUpdateError UI callback\n");
			if (onUpdateError) onUpdateError();
		}
		);
	}

private:

public:
};

#endif
