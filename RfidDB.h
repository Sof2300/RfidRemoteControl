#ifndef RFIDDB_H
#define RFIDDB_H

#include "RemoteSyncFile.h"
#include <vector>

#include "Definitions.h"

// External reference to the global SIMPLEFS instance
extern SimpleFSEsp32 SIMPLEFS;

struct StructuredTag {
	String tag, id, command;
	enum TagType {EMPTY,ID,COMMAND,ULTRACOMMAND,ERASE,UNDO,RFID_UNDO} type;

	StructuredTag(): type(EMPTY) {}
	StructuredTag(String tag0, String id0): tag(tag0), id(id0), type(EMPTY) {
		if(id0.length()==0) {
			Serial.println(String()+"Found no ID for this tag :'"+tag+"'");
			type=EMPTY;
		} else if(id0==ULTCOMMAND_KEYWORD){
			Serial.println(String()+"Found ULTRACOMMAND tag :"+id);
			type=ULTRACOMMAND;
		} else if(id0==ERASE_COMMAND_KEYWORD){
			Serial.println(String()+"Found ERASE tag :"+id);
			type=ERASE;
		} else if(id0==UNDO_COMMAND_KEYWORD){
			Serial.println(String()+"Found UNDO tag :"+id);
			type=UNDO;
		} else if(id0==RFID_UNDO_COMMAND_KEYWORD){
			Serial.println(String()+"Found RFID_UNDO tag :"+id);
			type=RFID_UNDO;
		} else if(id0.indexOf(COMMAND_KEYWORD)>-1) { // command tag
			command = id0;
			command.replace(String()+COMMANDDOT_KEYWORD,"");
			Serial.println(String()+"Found COMMAND: "+command);
			type=COMMAND;
		} else {	// id tag
			type=ID;
		}
	};
};

class RfidDB : public RemoteSyncFile {

	bool busy=false,synced=false, error=false;
	long syncts=0;

	bool patchInProgress=false;

	StructuredTag lastTag;

	// --- CACHE VARIABLES ---
	String lastFoundKey = "";
	String lastFoundValue = "";
	size_t lastFoundStart = 0;
	size_t lastFoundEnd = 0;

	// --- UNDO MECHANISM ---
	struct UndoOperation {
		enum OperationType {SET, ERASE} type;
		String tag;
		String oldId;
		String newId;
		size_t startPos;
		size_t endPos;
	};
	std::vector<UndoOperation> undoStack;
	static const int MAX_UNDO_OPERATIONS = 10;


protected:
	void onFileUpdated() override {
		Serial.println("RfidDB: File updated from server.");
	}

public:
	StructuredTag getLastTag(){return lastTag;}
	bool isBusy(){return busy;}
	bool isSynced(){return synced;}
	void init(String baseUrl, String filename){
		filename = String("/") + filename;
		Serial.println(String()+"rfiddb: init load "+filename);
		Serial.println(String()+"rfiddb: init content: '\n"+SIMPLEFS.readFileToString(filename)+"'");
		onUpdateBusy = [this]() {
			Serial.println("rfiddb: Updating...");
			busy=true;synced=false;error=false;
			if(patchInProgress) dispatchEventSM(EVENT_PATCH_INPROGRESS);
			else dispatchEventSM(EVENT_SYNC_INPROGRESS);
		};
		onUpdateSuccess = [this]() {
			Serial.println("rfiddb: Update Success!");
			busy=false; synced=true;error=false;syncts=millis();
			bool wasPatch=patchInProgress;
			patchInProgress=false;
			lastFoundKey="";
			if(wasPatch) dispatchEventSM(EVENT_PATCH_SUCCESS);
			else dispatchEventSM(EVENT_SYNC_SUCCESS);
		};
		onUpdateError = [this]() {
			Serial.println("rfiddb: Update Failed.");
			busy=false;synced=false;error=true;
			bool wasPatch=patchInProgress;
			patchInProgress=false;
			if(!reachableHttpCode(lastHttpCode)) dispatchEventSM(EVENT_SYNC_NOSUPERSCALE);
			else if(wasPatch) dispatchEventSM(EVENT_PATCH_ERROR);
			else dispatchEventSM(EVENT_SYNC_ERROR);
		};
		initSync(baseUrl, filename);
	}

	void wifiConnected() {
		syncServerDatabase();
	}

	void set(String tag, String id) {
		// This automatically checks cache first, then file.
		// It populates/preserves lastFoundStart and lastFoundEnd.
		String foundId = findInCSV(tag);

		// If it already exists with the exact same ID, skip writing
		if (foundId == id) {
			Serial.println("RfidDB: Tag already matches this ID. Doing nothing.");
			return;
		}

		String line = tag + "," + id + "\n";
		if (foundId.length() > 0) {
			// Exists but different ID -> Modifying
			Serial.println("RfidDB: Tag exists. Modifying record.");

			// Record undo operation before making changes
			UndoOperation op;
			op.type = UndoOperation::SET;
			op.tag = tag;
			op.oldId = foundId;
			op.newId = id;
			op.startPos = lastFoundStart;
			op.endPos = lastFoundEnd;
			pushUndoOperation(op);

			patchInProgress=true;
			submitModification(lastFoundStart, lastFoundEnd, line);
		} else {
			// Not found at all -> Appending
			Serial.println("RfidDB: Tag not found. Appending record.");
			size_t pos = getLocalSize();

			// Record undo operation for append
			UndoOperation op;
			op.type = UndoOperation::SET;
			op.tag = tag;
			op.oldId = "";
			op.newId = id;
			op.startPos = pos;
			op.endPos = pos + line.length();
			pushUndoOperation(op);
			Serial.printf("RfidDB: Recorded undo for append tag '%s' -> '%s' (range [%u..%u], len: %u)\n",
					tag.c_str(), id.c_str(), (unsigned)op.startPos, (unsigned)op.endPos, (unsigned)line.length());

			patchInProgress=true;
			submitModification(pos, pos, line);
		}
	}

	bool erase(String tag) {
		String id = findInCSV(tag);
		if (id.length() == 0) {
			Serial.println("RfidDB: tag not found");
			return false;
		}

		// Record undo operation before erasing
		UndoOperation op;
		op.type = UndoOperation::ERASE;
		op.tag = tag;
		op.oldId = id;
		op.newId = "";
		op.startPos = lastFoundStart;
		op.endPos = lastFoundEnd;
		pushUndoOperation(op);

		patchInProgress=true;
		submitModification(
				lastFoundStart,
				lastFoundEnd,
				""
		);
		return true;
	}




	String getID(String rfidtag) {
		String foundId = getValueByKey(rfidtag);
		if (foundId.length() > 0) Serial.println("RFID Key found: " + rfidtag + " -> " + foundId);
		else Serial.println("RFID Key not found: " + rfidtag);
		lastTag=StructuredTag(rfidtag, foundId);
		return lastTag.id;
	}

	String getRFID(String id) {
		return getKeyByValue(id);
	}

	// Check if an ID already exists in the database
	bool idExists(String id) {
		return getKeyByValue(id).length() > 0;
		/////////////////////////////////////////////////////
	}

	// --- UNDO MECHANISM FUNCTIONS ---
	void pushUndoOperation(UndoOperation op) {
		if (undoStack.size() >= MAX_UNDO_OPERATIONS) {
			undoStack.erase(undoStack.begin()); // Remove oldest operation
		}
		undoStack.push_back(op);
		Serial.println(String()+"RfidDB: Undo operation pushed. Stack size: "+undoStack.size());
	}

	bool canUndo() {
		return !undoStack.empty();
	}

	String getLastUndoDescription() {
		if (!canUndo()) {
			return "no operation to undo";
		}

		UndoOperation op = undoStack.back();
		if (op.type == UndoOperation::SET) {
			if (op.oldId.length() > 0) {
				// It was a modification
				return String() + "setting tag " + op.tag + " to " + op.newId;
			} else {
				// It was an append
				return String() + "adding tag " + op.tag + " as " + op.newId;
			}
		} else if (op.type == UndoOperation::ERASE) {
			return String() + "erasing tag " + op.tag + " (was " + op.oldId + ")";
		}
		return "unknown operation";
	}

	bool performUndo() {
		if (!canUndo()) {
			Serial.println("RfidDB: No undo operations available");
			return false;
		}

		UndoOperation op = undoStack.back();
		undoStack.pop_back();
		Serial.printf("RfidDB: Performing undo operation type: %d (tag: '%s', oldId: '%s', newId: '%s', stored range: [%u..%u])\n",
				(int)op.type, op.tag.c_str(), op.oldId.c_str(), op.newId.c_str(), (unsigned)op.startPos, (unsigned)op.endPos);

		if (op.type == UndoOperation::SET) {
			// Invalidate cache and dynamically find tag in CSV to get exact current start and end positions
			lastFoundKey = "";
			String currentId = findInCSV(op.tag);
			size_t start = (currentId.length() > 0) ? lastFoundStart : op.startPos;
			size_t end = (currentId.length() > 0) ? lastFoundEnd : op.endPos;

			if (currentId.length() > 0) {
				Serial.printf("RfidDB: Undo SET, located tag '%s' in CSV at [%u..%u] (current ID: '%s')\n",
						op.tag.c_str(), (unsigned)start, (unsigned)end, currentId.c_str());
			} else {
				Serial.printf("RfidDB: Undo SET, tag '%s' not found in CSV! Using stored range [%u..%u]\n",
						op.tag.c_str(), (unsigned)start, (unsigned)end);
			}

			if (op.oldId.length() > 0) {
				// It was a modification, revert to old ID
				String line = op.tag + "," + op.oldId + "\n";
				patchInProgress = true;
				Serial.printf("RfidDB: Undo SET, reverting %s to %s at [%u..%u]\n",
						op.tag.c_str(), op.oldId.c_str(), (unsigned)start, (unsigned)end);
				submitModification(start, end, line);
			} else {
				// It was an append, remove the line
				patchInProgress = true;
				Serial.printf("RfidDB: Undo SET, removing appended %s at [%u..%u]\n",
						op.tag.c_str(), (unsigned)start, (unsigned)end);
				submitModification(start, end, "");
			}
		} else if (op.type == UndoOperation::ERASE) {
			// Restore the erased entry
			String line = op.tag + "," + op.oldId + "\n";
			patchInProgress = true;
			Serial.printf("RfidDB: Undo ERASE, restoring %s with %s at [%u..%u]\n",
					op.tag.c_str(), op.oldId.c_str(), (unsigned)op.startPos, (unsigned)op.endPos);
			submitModification(op.startPos, op.endPos, line);
		}

		return true;
	}

	// --- 2. THE CSV BLOCK READER (RAM SAFE) ---
	// Searches the CSV file by reading in chunks of 1KB instead of loading the whole file
	String findInCSV(String searchKey) {
		// Check if we already have it in cache
		if (searchKey.length() > 0 && searchKey == lastFoundKey) return lastFoundValue;

		lastFoundKey = ""; // Invalidate cache first
		lastFoundValue="";
		lastFoundStart=0;
		lastFoundEnd=0;

		FileFS* f = SIMPLEFS.openFile(syncFilename.c_str(), "r");
		if (!f) {
			SYNC_LOG1("Failed to open CSV for reading.\n");
			return "";
		}

		const size_t BLOCK_SIZE = BLOCK_SIZE_DEFINE; // Read 1KB at a time
		char buffer[BLOCK_SIZE + 1];
		String carryOver = ""; // Holds partial lines between blocks

		size_t filePos = 0;

		String searchTarget = searchKey + ","; // We look for "TAG,"

		while (true) {
			size_t carryLength = carryOver.length(); // CHANGED: remember where carryOver starts
			size_t bytesRead = SIMPLEFS.readFile((unsigned char*)buffer, BLOCK_SIZE, f);
			if (bytesRead == 0) break;
			buffer[bytesRead] = '\0';
			size_t chunkStart = filePos - carryLength;
			String chunk = carryOver + String(buffer);
			filePos += bytesRead;

			// Find the last complete line in this block
			int lastNewline = chunk.lastIndexOf('\n');
			if (lastNewline != -1) {
				String processPart = chunk.substring(0, lastNewline);
				carryOver = chunk.substring(lastNewline + 1);

				// Search for the key in the completed lines
				int idx = 0;
				while ((idx = processPart.indexOf(searchTarget, idx)) != -1) {
					// Ensure it's at the start of a line to avoid partial matches
					if (idx == 0 || processPart.charAt(idx - 1) == '\n') {
						int valStart = idx + searchTarget.length();
						int endIdx = processPart.indexOf('\n', valStart);
						if (endIdx == -1) endIdx = processPart.length();
						SIMPLEFS.closeFile(f);
						lastFoundKey=searchKey;
						lastFoundValue= processPart.substring(valStart, endIdx); // Found the ID!
						lastFoundValue.trim();
						lastFoundStart = chunkStart + idx;
						lastFoundEnd = chunkStart + endIdx + 1;

						return lastFoundValue;
					}
					idx += searchTarget.length();
				}
			} else {
				// No newline in this entire block, just carry it over
				carryOver = chunk;
			}
		}

		SIMPLEFS.closeFile(f);

		// Check the very last line if it didn't end with a newline
		if (carryOver.startsWith(searchTarget)) {
			lastFoundKey = searchKey;
			lastFoundValue = carryOver.substring(searchTarget.length());
			lastFoundStart = filePos - carryOver.length();
			lastFoundEnd = filePos;
			lastFoundValue.trim();
			return lastFoundValue;
		}

		return ""; // Not found
	}


	String getValueByKey(String searchKey) {
		return findInCSV(searchKey);
	}

	String getKeyByValue(String searchValue) {
		FileFS* f = SIMPLEFS.openFile(syncFilename.c_str(), "r");
		if (!f) {
			SYNC_LOG1("Failed to open CSV for reverse lookup.\n");
			return "";
		}

		const size_t BLOCK_SIZE = 1024;
		char buffer[BLOCK_SIZE + 1];
		String carryOver = "";
		String valueSuffix = String(",") + searchValue;

		while (true) {
			size_t bytesRead = SIMPLEFS.readFile((unsigned char*)buffer, BLOCK_SIZE, f);
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
					String line = processPart.substring(lineStart, lineEnd);
					if (line.endsWith(valueSuffix)) {
						int commaIdx = line.indexOf(',');
						SIMPLEFS.closeFile(f);
						return commaIdx != -1 ? line.substring(0, commaIdx) : "";
					}
					lineStart = lineEnd + 1;
				}
			} else {
				carryOver = chunk;
			}
		}

		SIMPLEFS.closeFile(f);
		if (carryOver.endsWith(valueSuffix)) {
			int commaIdx = carryOver.indexOf(',');
			return commaIdx != -1 ? carryOver.substring(0, commaIdx) : "";
		}
		return "";
	}
};

#endif
