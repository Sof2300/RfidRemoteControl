#ifndef FILEUTILS_H
#define FILEUTILS_H

#include <rom/crc.h>
#include <ArduinoJson.h>


DynamicJsonDocument parseJSON(String filecontent) {
	DynamicJsonDocument doc(2500);//JsonDocument doc;
	DeserializationError error = deserializeJson(doc, filecontent);
	if (error) {
		Serial.println("Failed to parse config file. Using defaults.");
		return doc;
	}
	return doc;
}

String loadFileToString(String filename){	 Serial.println(String()+"loadJSON:: preread :"+"/"+filename);
	String filecontent=SIMPLEFS.readFileToString(String()+"/"+filename);
	//	Serial.println(String()+"File "+filename+" content1 :"+filecontent);
	filecontent=replaceAll(filecontent,"http://","XHTTP");
	//	Serial.println(String()+"File "+filename+" content0 :"+filecontent);	// comment incompatible with url "//"
	filecontent=removeComments(filecontent);
	//	Serial.println(String()+"File "+filename+" content-1 :"+filecontent);
	filecontent=replaceAll(filecontent,"XHTTP","http://");
	if (filecontent.length()==0) {
		Serial.println(String()+"File "+filename+" content empty.");
	}
	//	Serial.println(String()+"File "+filename+" content :"+filecontent);
	return filecontent;
}

DynamicJsonDocument loadJSON(String filename) {
	return parseJSON(loadFileToString(filename));
}

//////////////


String getFileCRC(const String &name, size_t limit=0) {
	FileFS* f = SIMPLEFS.openFile(name.c_str(), "r");
	if (!f) {
		char hex[9];
		snprintf(hex, sizeof(hex), "%08lx", (unsigned long)0);
		return String(hex);
	}
	if(limit==0) limit=f->size;
	const size_t BLOCK_SIZE = 1024;
	uint8_t buffer[BLOCK_SIZE];
	uint32_t crc = 0;
	size_t totalRead = 0;
	while (totalRead < limit) {
		size_t toRead = BLOCK_SIZE;
		if (totalRead + toRead > limit) toRead = limit - totalRead;
		unsigned long bytesRead = SIMPLEFS.readFile(buffer, toRead, f);
		if (bytesRead == 0) break;
		crc = crc32_le(crc, buffer, bytesRead);
		totalRead += bytesRead;
	}
	SIMPLEFS.closeFile(f);
	char hex[9];
	snprintf(hex, sizeof(hex), "%08lx", (unsigned long)crc);
	return String(hex);
}




// --- FUNCTION 2: IN-PLACE SPLICER ---
// Modifies the exact bytes without creating a temporary file
/*
 AI critical comments about the following I don't understand:
When the splice shortens the file and the modified region straddles or is after lastSyncedSize, the value can become inconsistent.
Padding with spaces when shrinking leaves garbage that later CRC / size calculations still see until the next full rewrite.
No file truncation after a shrink → file size reported by the FS can stay larger than the logical content.
 * */

#define CHUNK 1024

bool spliceFileInPlace(String syncFilename, size_t startOffset, size_t endOffset, String newContent) {
	Serial.println("[SYNC] Spliced IN-PLACE started\n");
	size_t beforeSize = SIMPLEFS.fileSize(syncFilename.c_str());

	FileFS* f = SIMPLEFS.openFile(syncFilename.c_str(), "r+");
	if (!f) return false;

	SimpleFileFS* sf = (SimpleFileFS*)f;
	File& file = sf->fileptr;

	int diff = (int)newContent.length() - (int)(endOffset - startOffset);
	size_t tailSize = beforeSize - endOffset;

	if (diff > 0) {
		// SHIFT RIGHT (Iterate Backwards)
//		const size_t CHUNK = 512;
		uint8_t buf[CHUNK+8];
		size_t bytesToShift = tailSize;

		while (bytesToShift > 0) {
			size_t toRead = (bytesToShift > CHUNK) ? CHUNK : bytesToShift;
			size_t readPos = endOffset + bytesToShift - toRead;
			size_t writePos = readPos + diff;

			file.seek(readPos, SeekSet);
			file.read(buf, toRead);
			file.seek(writePos, SeekSet);
			file.write(buf, toRead);

			bytesToShift -= toRead;
		}
	} else if (diff < 0) {
		// SHIFT LEFT (Iterate Forwards)
		const size_t CHUNK = 512;
		uint8_t buf[CHUNK];
		size_t bytesShifted = 0;

		while (bytesShifted < tailSize) {
			size_t toRead = (tailSize - bytesShifted > CHUNK) ? CHUNK : (tailSize - bytesShifted);
			size_t readPos = endOffset + bytesShifted;
			size_t writePos = readPos + diff;

			file.seek(readPos, SeekSet);
			file.read(buf, toRead);
			file.seek(writePos, SeekSet);
			file.write(buf, toRead);

			bytesShifted += toRead;
		}
	}

	// Insert the new record into the gap
	file.seek(startOffset, SeekSet);
	file.print(newContent);

	// Pad garbage with spaces if shrunk
	if (diff < 0) {
		file.seek(beforeSize + diff, SeekSet);
		for (int i = 0; i < -diff; i++) file.write(' ');
	}

	SIMPLEFS.closeFile(f);

	Serial.println("[SYNC] Spliced IN-PLACE OK. Diff: %d bytes.\n", diff);
	return true;
}





#endif
