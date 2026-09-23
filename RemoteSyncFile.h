#ifndef REMOTESYNCFILE_H
#define REMOTESYNCFILE_H

#include <functional>
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
			return 0;
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

			return 1;
		}
	}
protected:
//	virtual bool checkDeltaConflict(const String& serverDelta, const String& clientDelta)=0;
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
				SYNC_LOG1("[SYNC] -> FULL_UPDATE dispatched (base CRC mismatch; server wins)\n");
				SYNC_LOG1("[SYNC] ==== syncServerDatabase END (->downloadFull) ====\n");
				downloadFull();
			}
			else if (response.startsWith("PULL_DELTA:")) {
				String tail = response.substring(strlen("PULL_DELTA:"));
				tail.trim();
				long sts = tail.toInt();
				if (sts <= 0 || (size_t)sts < checkSize) {
					SYNC_LOG1("[SYNC] -> PULL_DELTA has bad serverTotalSize=%ld checkSize=%u -> fallback FULL_UPDATE\n",
							sts, (unsigned)checkSize);
					SYNC_LOG1("[SYNC] ==== syncServerDatabase END (->downloadFull fallback) ====\n");
					downloadFull();
				} else {
					size_t serverTotalSize = (size_t)sts;
					bool bothSidesAppended = (currentSize > checkSize);
					SYNC_LOG2("[SYNC] -> PULL_DELTA dispatched. base=%u serverSize=%u clientSize=%u bothSides=%s\n",
							(unsigned)checkSize, (unsigned)serverTotalSize, (unsigned)currentSize,
							bothSidesAppended ? "YES(client+server both appended)" : "NO(server-only append)");
					SYNC_LOG1("[SYNC] ==== syncServerDatabase END (->performMerge) ====\n");
					performMerge(checkSize, serverTotalSize, currentSize);
				}
			}
			else if (response == "PUSH_DELTA") {
				SYNC_LOG2("[SYNC] -> PUSH_DELTA dispatched. unpushedBytes=%u\n",
						currentSize > lastSyncedSize ? (unsigned)(currentSize - lastSyncedSize) : 0u);
				SYNC_LOG1("[SYNC] ==== syncServerDatabase END (->pushClientDelta) ====\n");
				pushClientDelta();
			}
			else {
				SYNC_LOG1("[SYNC] -> unrecognized response -> onUpdateError\n");
				this->lastErrorMsg = "Unrecognized response: " + response;
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
		}, SYNC_TIMEOUT
		);
	}



	bool checkDeltaConflict(const String& serverDelta, const String& clientDelta) {
		bool b=checkDeltaConflict(serverDelta,clientDelta,0);
		b= b || checkDeltaConflict(serverDelta,clientDelta,1);
		return b;
	}
	// Helper function to check for overlapping IDs in two CSV delta strings
	bool checkDeltaConflict(const String& serverDelta, const String& clientDelta,int idColumnIndex) {
		// NOTE: Change ',' and '0' if your ID is in a different column!
		// e.g., if ID is the 2nd column, change 0 to 1.
		// Lambda to extract the ID from a single CSV line
		char delimiter = ',';
		auto extractId = [delimiter, idColumnIndex](const String& line) -> String {
			int currentColumn = 0;
			int startIndex = 0;
			for (unsigned int i = 0; i <= line.length(); i++) {
				if (i == line.length() || line.charAt(i) == delimiter) {
					if (currentColumn == idColumnIndex) {
						String id = line.substring(startIndex, i);
						id.trim(); // Remove any stray \r or spaces
						return id;
					}
					currentColumn++;
					startIndex = i + 1;
				}
			}
			return "";
		};

		// 1. Extract all IDs from the Server Delta
		std::vector<String> serverIds;
		int s = 0;
		while (s < serverDelta.length()) {
			int e = serverDelta.indexOf('\n', s);
			if (e == -1) e = serverDelta.length();

			String line = serverDelta.substring(s, e);
			if (line.length() > 0) {
				String id = extractId(line);
				if (id.length() > 0) {
					serverIds.push_back(id);
				}
			}
			s = e + 1;
		}

		// 2. Check Client Delta IDs against Server IDs
		s = 0;
		while (s < clientDelta.length()) {
			int e = clientDelta.indexOf('\n', s);
			if (e == -1) e = clientDelta.length();

			String line = clientDelta.substring(s, e);
			if (line.length() > 0) {
				String id = extractId(line);
				if (id.length() > 0) {
					// Check if this ID exists in the server IDs
					for (const String& sid : serverIds) {
						if (sid == id) return true; // CONFLICT FOUND!
					}
				}
			}
			s = e + 1;
		}

		return false; // No conflicts
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
		SYNC_LOG1("[SYNC] submitModification PATCH - url:\n",reqUrl);

		SYNC_LOG2("[SYNC] submitModification PATCH -> %s (If-Match: %s, Range: %u-%u/%u)\n",
				reqUrl.c_str(), currentCrc.c_str(), (unsigned)start, (unsigned)end, (unsigned)currentSize);

		AsyncHttpRequest::request("PATCH", reqUrl, content, headers,
				[this, start, end, content](const StreamString *response, AsyncHttpRequest *req) {
			SYNC_LOG1("[SYNC] Server accepted PATCH. Applying locally...\n");
			String oldCrc=getFileCRC(syncFilename);
			bool success = spliceFileInPlace(syncFilename, start, end, content);
			if (success) {
				lastSyncedSize=SIMPLEFS.fileSize(syncFilename.c_str());
				notifyFileUpdated("submitModification");
				if (onUpdateSuccess) onUpdateSuccess();
			} else {
				SYNC_LOG1("[SYNC] ERROR: Server accepted, but local write failed!\n");
				if (onUpdateError) onUpdateError();
			}
		},
		[this](const String &error, AsyncHttpRequest *req) {
			this->lastErrorMsg = error;
			this->lastHttpCode = req ? req->responseHTTPcode() : 0;
			SYNC_LOG1("[SYNC] PATCH Failed (HTTP %d): %s\n", this->lastHttpCode, error.c_str());

			// 412 = Precondition Failed (Someone else changed the file first)
			if (this->lastHttpCode == 412) {
				SYNC_LOG1("[SYNC] CRC Mismatch! Server file changed. Triggering sync to heal...\n");
				syncServerDatabase();
			}
			// Check for duplicate ID error from server
			else if (error.indexOf("DUPLICATE_ID") != -1) {
				SYNC_LOG1("[SYNC] Duplicate ID conflict detected by server. Rejecting operation...\n");
				if (onDuplicateIdError) onDuplicateIdError();
				else if (onUpdateError) onUpdateError();
			}
			else {
				if (onUpdateError) onUpdateError();
			}
		}, SYNC_TIMEOUT
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

	// --- PUSH CLIENT DELTA ---
	void pushClientDelta() {
		SYNC_LOG1("[SYNC] pushClientDelta START\n");
		size_t currentSize = getLocalSize();
		if (currentSize <= lastSyncedSize) {
			SYNC_LOG2("[SYNC] pushClientDelta: NO-OP (currentSize=%u <= lastSyncedSize=%u)\n",
					(unsigned)currentSize, (unsigned)lastSyncedSize);
			SYNC_LOG1("[SYNC] pushClientDelta END (no-op)\n");
			return;
		}

		SYNC_LOG2("[SYNC] pushClientDelta: unpushed bytes range [%u .. %u] = %u bytes. localCRC@pushBase=%s\n",
				(unsigned)lastSyncedSize, (unsigned)currentSize, (unsigned)(currentSize - lastSyncedSize),
				getLocalCRC(lastSyncedSize).c_str());

		FileFS* f = SIMPLEFS.openFile(syncFilename.c_str(), "r");
		if (!f) {
			SYNC_LOG1("[SYNC] pushClientDelta: FAIL cannot open file for read\n");
			if (onUpdateError) onUpdateError();
			return;
		}
		SIMPLEFS.seekFile(lastSyncedSize, f);
		SimpleFileFS* sf = (SimpleFileFS*)f;
		if (lastSyncedSize > sf->size) {
			SIMPLEFS.closeFile(f);
			SYNC_LOG1("[SYNC] pushClientDelta: FAIL seek(%u) out of range file.size=%u\n",
					(unsigned)lastSyncedSize, (unsigned)sf->size);
			if (onUpdateError) onUpdateError();
			return;
		}
		size_t deltaLen = currentSize - lastSyncedSize;
		String clientDeltaStr;
		clientDeltaStr.reserve(deltaLen + 64);
		const size_t BLOCK = 1024;
		uint8_t buf[BLOCK];
		size_t got = 0;
		while (got < deltaLen) {
			size_t toRead = deltaLen - got;
			if (toRead > BLOCK) toRead = BLOCK;
			unsigned long r = SIMPLEFS.readFile(buf, toRead, f);
			if (r == 0) break;
			clientDeltaStr.concat((const char *)buf, (size_t)r);
			got += (size_t)r;
		}
		SIMPLEFS.closeFile(f);
		SYNC_LOG2("[SYNC] pushClientDelta: read %u/%u bytes from disk into RAM delta buffer\n",
				(unsigned)got, (unsigned)deltaLen);

		// Count number of CSV records (lines) being pushed
		unsigned pushedRecords = 0;
		{
			int s = 0;
			while ((s = clientDeltaStr.indexOf('\n', s)) != -1) { pushedRecords++; s++; }
			if (clientDeltaStr.length() > 0 &&
					(clientDeltaStr.charAt(clientDeltaStr.length() - 1) != '\n')) {
				pushedRecords++;
			}
		}
		SYNC_LOG2("[SYNC] pushClientDelta: payload preview (%u records, %u bytes): first120='%s'\n",
				pushedRecords, (unsigned)clientDeltaStr.length(),
				clientDeltaStr.substring(0, 120).c_str());

		String pushUrl = syncUrl + "sync_push_delta?offset=" + String(lastSyncedSize);
		SYNC_LOG2("[SYNC] pushClientDelta: POST %u bytes -> %s\n",
				(unsigned)clientDeltaStr.length(), pushUrl.c_str());
		SYNC_LOG2("[SYNC] POST on %s\n", pushUrl.c_str());
		AsyncHttpRequest::post(
				pushUrl,
				clientDeltaStr,
				[this, currentSize](const StreamString *content, AsyncHttpRequest *req) {
			String respStr = content ? content->c_str() : String("");
			respStr.trim();
			SYNC_LOG2("[SYNC] pushClientDelta: server response status=OK body(%u bytes)='%s'\n",
					(unsigned)respStr.length(),
					respStr.length() > 160 ? (respStr.substring(0, 160) + "...").c_str() : respStr.c_str());
			lastSyncedSize = currentSize;
			size_t sz = getLocalSize();
			SYNC_LOG1("[SYNC] pushClientDelta SUCCESS. fileSize=%u CRC=%s lastSyncedSize=%u\n",
					(unsigned)sz, getLocalCRC(sz).c_str(), (unsigned)lastSyncedSize);
			SYNC_LOG1("[SYNC] pushClientDelta END (OK)\n");
			if (onUpdateSuccess) {
				SYNC_LOG2("[SYNC] pushClientDelta: firing onUpdateSuccess UI callback\n");
				onUpdateSuccess();
			}
		},
		[this](const String &error, AsyncHttpRequest *req) {
			SYNC_LOG1("[SYNC] pushClientDelta: POST FAILED. %s\n", error.c_str());
			SYNC_LOG2("[SYNC] pushClientDelta: local state unchanged. lastSyncedSize=%u currentSize=%u\n",
					(unsigned)lastSyncedSize, (unsigned)getLocalSize());
			this->lastErrorMsg = error;
			this->lastHttpCode = req ? req->responseHTTPcode() : 0;
			SYNC_LOG1("[SYNC] pushClientDelta END (ERROR)\n");
			if (onUpdateError) onUpdateError();
		}, SYNC_TIMEOUT
		);
	}

	// --- RAM-SWAP MERGE (Pull-then-Push for concurrent appends)
	void performMerge(size_t baseSize, size_t serverTotalSize, size_t clientTotalSize) {
		SYNC_LOG1("[SYNC] performMerge START (pull then push)\n");
		bool clientHasDelta = (clientTotalSize > baseSize);
		size_t serverDeltaLen = serverTotalSize - baseSize;
		SYNC_LOG2("[SYNC] performMerge args: base=%u serverSize=%u serverDelta=%u clientSize=%u clientAppended=%s clientDelta=%u\n",
				(unsigned)baseSize, (unsigned)serverTotalSize, (unsigned)serverDeltaLen,
				(unsigned)clientTotalSize,
				clientHasDelta ? "YES" : "NO",
						clientHasDelta ? (unsigned)(clientTotalSize - baseSize) : 0u);

		if (onUpdateBusy) onUpdateBusy();

		String clientDeltaStr = "";

		if (clientHasDelta) {
			FileFS* f = SIMPLEFS.openFile(syncFilename.c_str(), "r");
			if (!f) {
				SYNC_LOG1("[SYNC] performMerge: FAIL cannot open source CSV -> fallback to FULL_UPDATE\n");
				downloadFull();
				return;
			}
			SIMPLEFS.seekFile(baseSize, f);
			SimpleFileFS* sf = (SimpleFileFS*)f;
			if (baseSize > sf->size) {
				SIMPLEFS.closeFile(f);
				SYNC_LOG1("[SYNC] performMerge: FAIL seek(base=%u) file.size=%u -> fallback FULL_UPDATE\n",
						(unsigned)baseSize, (unsigned)sf->size);
				downloadFull();
				return;
			}
			size_t clientDeltaLen = clientTotalSize - baseSize;
			clientDeltaStr.reserve(clientDeltaLen + 64);
			const size_t BLOCK = 1024;
			uint8_t buf[BLOCK];
			size_t got = 0;
			while (got < clientDeltaLen) {
				size_t toRead = clientDeltaLen - got;
				if (toRead > BLOCK) toRead = BLOCK;
				unsigned long r = SIMPLEFS.readFile(buf, toRead, f);
				if (r == 0) break;
				clientDeltaStr.concat((const char *)buf, (size_t)r);
				got += (size_t)r;
			}
			SIMPLEFS.closeFile(f);
			unsigned clientRecords = 0;
			{
				int s = 0;
				while ((s = clientDeltaStr.indexOf('\n', s)) != -1) { clientRecords++; s++; }
				if (clientDeltaStr.length() > 0 && clientDeltaStr.charAt(clientDeltaStr.length() - 1) != '\n')
					clientRecords++;
			}
			SYNC_LOG2("[SYNC] performMerge: SNAPSHOT client delta bytes[%u..%u] %u bytes / %u records read into RAM. preview='%s'\n",
					(unsigned)baseSize, (unsigned)clientTotalSize,
					(unsigned)clientDeltaStr.length(), clientRecords,
					clientDeltaStr.substring(0, 120).c_str());
		}

		String deltaUrl = syncUrl + "sync_download_delta?offset=" + String(baseSize);
		SYNC_LOG2("[SYNC] performMerge: GET server delta bytes[%u..%u] (%u bytes) -> %s\n",
				(unsigned)baseSize, (unsigned)serverTotalSize, (unsigned)serverDeltaLen, deltaUrl.c_str());

		AsyncHttpRequest::fetch(
				deltaUrl,
				[this, baseSize, serverTotalSize, clientTotalSize, clientHasDelta, clientDeltaStr, serverDeltaLen](const StreamString *content, AsyncHttpRequest *req) mutable {
			String serverDeltaStr = content->c_str();
			unsigned serverRecords = 0;
			{
				int s = 0;
				while ((s = serverDeltaStr.indexOf('\n', s)) != -1) { serverRecords++; s++; }
				if (serverDeltaStr.length() > 0 && serverDeltaStr.charAt(serverDeltaStr.length() - 1) != '\n')
					serverRecords++;
			}
			SYNC_LOG2("[SYNC] performMerge: server delta received %u/%u bytes, %u records. preview='%s'\n",
					(unsigned)serverDeltaStr.length(), (unsigned)serverDeltaLen, serverRecords,
					serverDeltaStr.substring(0, 120).c_str());
			if (serverDeltaStr.length() != serverDeltaLen) {
				SYNC_LOG1("[SYNC] performMerge: WARNING server payload length mismatch (got %u expected %u); proceeding anyway\n",
						(unsigned)serverDeltaStr.length(), (unsigned)serverDeltaLen);
			}
			if (clientHasDelta && serverDeltaStr.length() > 0 && clientDeltaStr.length() > 0) {

				bool hasConflict = checkDeltaConflict(serverDeltaStr, clientDeltaStr);

				if (hasConflict) {
					SYNC_LOG1("[SYNC] performMerge: CONFLICT DETECTED! Same ID in client and server deltas. Dropping client delta (Server Wins).\n");
					clientHasDelta = false;
					clientDeltaStr = "";
				} else {
					SYNC_LOG1("[SYNC] performMerge: No ID conflicts detected. Safe to merge both.\n");
				}
			}
			// Direct in-place merge: open in "r+" to overwrite starting at baseSize without truncating the base
			{
				FileFS* dst = SIMPLEFS.openFile(syncFilename.c_str(), "r+");
				if (!dst) {
					SYNC_LOG1("[SYNC] performMerge: FAIL cannot open file for r+ -> FULL_UPDATE fallback\n");
					downloadFull();
					return;
				}
				SimpleFileFS* sdst = (SimpleFileFS*)dst;

				// Seek to the end of the common base
				sdst->fileptr.seek(baseSize, SeekSet);

				// Append server delta
				if (serverDeltaStr.length() > 0) {
					sdst->fileptr.print(serverDeltaStr);
					SYNC_LOG2("[SYNC] performMerge: in-place APPEND server delta %u bytes (@offset=%u)\n",
							(unsigned)serverDeltaStr.length(), (unsigned)baseSize);
				} else {
					SYNC_LOG2("[SYNC] performMerge: no server delta to append\n");
				}

				// Append client delta
				if (clientHasDelta && clientDeltaStr.length() > 0) {
					sdst->fileptr.print(clientDeltaStr);
					SYNC_LOG2("[SYNC] performMerge: in-place APPEND client delta %u bytes (@offset=%u)\n",
							(unsigned)clientDeltaStr.length(), (unsigned)(baseSize + serverDeltaStr.length()));
				} else if (clientHasDelta) {
					SYNC_LOG2("[SYNC] performMerge: clientHasDelta but delta is empty -> skip\n");
				} else {
					SYNC_LOG2("[SYNC] performMerge: no client delta to append (server-only case)\n");
				}

				SIMPLEFS.closeFile(dst);
			}

			size_t expectedTmpSize = baseSize + serverDeltaStr.length() + (clientHasDelta ? clientDeltaStr.length() : 0);
			size_t afterSize = getLocalSize();
			String afterCRC = getLocalCRC(afterSize);
			lastSyncedSize = serverTotalSize;

			SYNC_LOG1("[SYNC] performMerge: merged file OK. expectedSize=%u finalSize=%u CRC=%s. lastSyncedSize set to serverSize=%u. "
					"pushed-client-records-still-unpushed=%u bytes\n",
					(unsigned)expectedTmpSize, (unsigned)afterSize, afterCRC.c_str(), (unsigned)lastSyncedSize,
					afterSize > lastSyncedSize ? (unsigned)(afterSize - lastSyncedSize) : 0u);

			notifyFileUpdated("performMerge");
			if (onUpdateSuccess) {
				SYNC_LOG2("[SYNC] performMerge: firing onUpdateSuccess UI callback\n");
				onUpdateSuccess();
			}
			SYNC_LOG1("[SYNC] performMerge END -> triggering pushClientDelta to publish merged tail\n");
			pushClientDelta();
		},
		[this, baseSize, serverTotalSize, clientTotalSize](const String &error, AsyncHttpRequest *req) {
			SYNC_LOG1("[SYNC] performMerge: FAIL fetch server delta bytes[%u..%u] error=%s\n",
					(unsigned)baseSize, (unsigned)serverTotalSize, error.c_str());
			SYNC_LOG2("[SYNC] performMerge: local file unchanged. currentSize=%u lastSyncedSize=%u\n",
					(unsigned)getLocalSize(), (unsigned)lastSyncedSize);
			this->lastErrorMsg = error;
			this->lastHttpCode = req ? req->responseHTTPcode() : 0;
			SYNC_LOG1("[SYNC] performMerge END (NETWORK ERROR)\n");
			if (onUpdateError) onUpdateError();
		}, SYNC_TIMEOUT
		);
	}

private:

public:
	// --- DELTA APPEND ---
	void downloadDelta(/*int newMod, size_t newOffset*/) {
		size_t beforeSize = getLocalSize();
		String reqUrl = syncUrl + "sync_download?offset=" + String(beforeSize);
		SYNC_LOG2("[SYNC] downloadDelta START. beforeSize=%u beforeCRC=%s GET -> %s\n",
				(unsigned)beforeSize, getLocalCRC(beforeSize).c_str(), reqUrl.c_str());

		if (onUpdateBusy) onUpdateBusy();

		FileFS* f = SIMPLEFS.openFile(syncFilename.c_str(), "a");
		if (!f) {
			SYNC_LOG1("[SYNC] downloadDelta: FAIL cannot open file for append\n");
			if (onUpdateError) onUpdateError();
			return;
		}
		SimpleFileFS* sf = (SimpleFileFS*)f;
		AsyncHttpRequest::fetchToFile(
				reqUrl,
				sf->fileptr,
				[this, beforeSize, f](const StreamString *content, AsyncHttpRequest *req) {
			// File is closed automatically by AsyncHttpRequest when done
			(void)content; // body streamed to file in chunks; no RAM buffer
			size_t afterSize = getLocalSize();
			lastSyncedSize = afterSize;
			SYNC_LOG1("[SYNC] downloadDelta SUCCESS. beforeSize=%u afterSize=%u (+%u bytes) afterCRC=%s lastSyncedSize=%u\n",
					(unsigned)beforeSize, (unsigned)afterSize, (unsigned)(afterSize - beforeSize),
					getLocalCRC(afterSize).c_str(),
					(unsigned)lastSyncedSize);
			notifyFileUpdated("downloadDelta");
			if (onUpdateSuccess) {
				SYNC_LOG2("[SYNC] downloadDelta: firing onUpdateSuccess UI callback\n");
				onUpdateSuccess();
			}
		},
		[this, beforeSize, f](const String &error, AsyncHttpRequest *req) {
			// File is closed automatically by AsyncHttpRequest when done
			SYNC_LOG1("[SYNC] downloadDelta FAILED. beforeSize=%u error=%s\n",
					(unsigned)beforeSize, error.c_str());
			this->lastErrorMsg = error;
			this->lastHttpCode = req ? req->responseHTTPcode() : 0;
			if (onUpdateError) onUpdateError();
		}, SYNC_TIMEOUT
		);
	}

	/*
	void parseVersionString(String verStr, int &mod, size_t &offset) {
		int commaIdx = verStr.indexOf(',');
		if (commaIdx != -1) {
			mod = verStr.substring(0, commaIdx).toInt();
			offset = verStr.substring(commaIdx + 1,verStr.length()).toInt();
		}
	}
	 */
};

#endif
