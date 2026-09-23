#ifndef RFIDDB_H
#define RFIDDB_H

#include "RemoteSyncFile.h"


DynamicJsonDocument parseJSON(String filecontent) {
	DynamicJsonDocument doc(2500);//JsonDocument doc;
	DeserializationError error = deserializeJson(doc, filecontent);
	if (error) {
		Serial.println("Failed to parse config file. Using defaults.");
		return doc;
	}
	return doc;
}

String loadFileToString(String filename){//	 Serial.println(String()+"loadJSON:: preread :"+"/"+filename);
	String filecontent=SimpleFS.readFileToString(String()+"/"+filename);
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



class RfidDB : public RemoteSyncFile {
//	std::map<String,String> rfid2id;
	String rfidFilename;
	String baseUrl;

protected:
	// Implementation of the parent's pure virtual function
	void onFileUpdated() override {
		Serial.println("RfidDB: File updated from server, reloading map...");
//		loadFromFile(); // Reparses the JSON and updates the std::map in real-time
	}

public:
	void set(String tag, String id) {
//		rfid2id[tag] = id;
		Serial.println(String()+"RfidDB::set map modified, new map:\n"+toString());
	}

	void setFilename(String filename){
		rfidFilename = filename;
	}

	bool erase(String tag){
//		bool removed=rfid2id.erase(tag);
		if(removed) Serial.println(String()+"RfidDB removed tag with success :"+tag);
		else Serial.println(String()+"RfidDB failed to remove tag :"+tag);
		return removed;

	}

	void init(String baseurl0, String filename){
		baseUrl=baseurl0;
		setFilename(filename);
		// Wire up Audio/Visual feedback for synchronization
		onUpdateBusy = [this]() {
			Serial.println(String()+"rfiddb.onUpdateBusy busy");
//			if(rgbled) rgbled->playColor(LED_BUSY_COLOR);
		};
		onUpdateSuccess = [this]() {
			Serial.println(String()+"rfiddb.onUpdateSuccess busy");
//			if(vbeep) vbeep->playSpecial("reconnu");
//			if(rgbled) rgbled->playColor(LED_WIFI_CONNECTED_COLOR);
		};
		onUpdateError = [this]() {
			Serial.println(String()+"rfiddb.onUpdateError busy");
//			errorAV();
		};
		initSync(baseUrl, filename);
	}
	void wifiConnected(){
		syncServerDatabase();
	}
/*
	bool loadFromFile(){
		String filename=rfidFilename;
		DynamicJsonDocument doc=loadJSON(filename);
		JsonObject  root = doc.as<JsonObject>();
		bool changed=false;
		for (JsonPair kv : root) {
			String key=kv.key().c_str();String val=kv.value().as<const char*>();
			rfid2id[key]=val;
			Serial.println(String()+ "Loading RFID file > " + key+": "+val);
			changed=true;
		}
		Serial.println(String()+"RfidDB::loadFromFile map modified, new map:\n"+toString());
		return changed;
	}
	void saveToFile() {
		String filename=rfidFilename;
		String output="{";
		bool first=true;
		for (auto const& pair : rfid2id) {
			if(first) first=false;
			else output+=",\n";
			output+=String()+"\""+pair.first+"\":\""+ pair.second+"\"";
		}
		output+="\n}";
		Serial.println(String()+ "saveToFile output" + output);
		//		SimpleFS.rewriteFile(String()+"/"+filename,output);
	}

	String getID(String rfidtag){
		auto it = rfid2id.find(rfidtag);
		if (it != rfid2id.end()) {
			Serial.println(String()+"RFID Key found: "+ it->first +" -> "+ it->second );
			return rfid2id[rfidtag];
		} else {
			Serial.println(String()+ "RFID Key not found: " + rfidtag);
			return "";
		}
	}*/
	String getID(String rfidtag){
	    String foundId = findInCSV(rfidtag);
	    if (foundId.length() > 0) {
	        Serial.println("RFID Key found: " + rfidtag + " -> " + foundId);
	        return foundId;
	    } else {
	        Serial.println("RFID Key not found: " + rfidtag);
	        return "";
	    }
	}
	String getRFID(String id) {
		for (auto const& pair : rfid2id) {
			if (pair.second == id) return pair.first; // Found it, return the RFID tag
		}
		return ""; // Not found, return empty string
	}
	String toString() {
		String s="";
		for (auto const& pair : rfid2id) {
			s+="-\""+pair.first+"\":\""+pair.second+"\",\n";
		}
		return s; // Not found, return empty string
	}
} ;


#endif
