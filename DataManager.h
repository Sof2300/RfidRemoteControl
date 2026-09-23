#ifndef DATAMANAGER_H
#define DATAMANAGER_H

#include <map>

#include <WiFi.h>

#include <fs/SimpleFS.h>

#include "AsyncHttpRequest.h"

#include "RfidDB.h"

#include "RGBLed.h"

#include "Definitions.h"

#include "FileUtils.h"


RfidDB rfiddb;




////////////////////////////////////////////////////////////////////////////////////////////////////

class DataManager {
	//	VBeeper *vbeep=0;
	//	RGBLed *rgbled;

	std::map<String, String> configMap;

	String currentTag, currentID;
	bool isNewID=false;
	//	asyncHTTPrequest saveRequest;
	bool notifyNext=false;
	bool saved=false;

	bool newid=false, knownid=false;

	String baseUrl;
	bool requestbusy=false;

	bool idset=false;
	bool eraseMode=false;

	String currentCommand;

	//	AsyncHttpClient httpClient;

public:

	//	void setVBeeper(VBeeper *beeper){vbeep=beeper;}
	//	void setRGBLed(RGBLed *led){rgbled=led;}

	String getCurrentTag(){return currentTag;}
	String getCurrentID(){return currentID;}
	void fetchSetId() {fetchSetId(currentID);}
	String getCurrentCommand(){return currentCommand;}

	// Check if the generated ID already exists in the local database
	bool validateGeneratedId(String id) {
		return !rfiddb.idExists(id);
	}

	String getConfig(String key) {	// --- get a config value by key ---
		auto it = configMap.find(key);
		if (it != configMap.end()) {
			return it->second;
		}
		return ""; // Return empty string if key is not found
	}
	void saveCurrentRfid(String id=""){
		if(id.length()==0) id=getCurrentID();
		//requestbusy=true;
		rfiddb.set(getCurrentTag(), id);	//will sync automatically
	}
	void eraseCurrentRfid(){
		//requestbusy=true;
		rfiddb.erase(getCurrentTag());	//will sync automatically
	}
	void clearRequestBusy(){requestbusy=false;}


	void init(String configfilename, String rfidfilename) {
		loadConfig(configfilename);
		// here must attempt to load from superscale
		baseUrl = getConfig(SUPERSCALE_URL_FIELD);
		if (baseUrl.length() == 0) {
			Serial.println(String()+"Error: '"+SUPERSCALE_URL_FIELD+"' not found in config.json!");
			//bigErrorAV();
			dispatchEventSM(EVENT_NOSUPERSCALE_CONFIG);
			return;
		}
		if(baseUrl[baseUrl.length()-1]!='/') baseUrl+="/";
		rfiddb.init(baseUrl,rfidfilename);
	}

	void wifiConnected(){
		rfiddb.wifiConnected();
	}


	void noTagFound(){


	}

	void tagFound(String tag){
		tag.replace(" ","");
		Serial.println(String()+"------------- RFID detected: '"+tag+"'");

		if(//tag==currentTag ||
				requestbusy || rfiddb.isBusy() || !rfiddb.isSynced()) {
/*			if(tag==currentTag) {
				Serial.println(String()+"Same tag scanned again, will be ignored !");
				dispatchEventSM(EVENT_RFID_SAMETAGAGAIN);
			}
*/			if(requestbusy) Serial.println(String()+"Busy waiting for superscale reply !");
			if(rfiddb.isBusy()) Serial.println(String()+"Busy waiting for superscale sync !");
			if(!rfiddb.isSynced()) Serial.println(String()+"RFIDDB not synced !");
			dispatchEventSM(EVENT_RFID_BUSY);
			return;
		}
		saved=false;
		String id=rfiddb.getID(tag);
		currentTag = tag;
		currentID="";
		StructuredTag structag = rfiddb.getLastTag();
		if(structag.type==StructuredTag::ULTRACOMMAND) {
			currentCommand="";
			dispatchEventSM(EVENT_RFID_ATTACHCOMMAND);
		} else if(structag.type==StructuredTag::ERASE) {
			dispatchEventSM(EVENT_RFID_ERASE);
		} else if(structag.type==StructuredTag::COMMAND) {
			currentCommand=structag.command;
			dispatchEventSM(EVENT_RFID_COMMAND);
		} else if(structag.type==StructuredTag::ID) {
			currentID = id;
			isNewID = false;
			currentCommand="";
			dispatchEventSM(EVENT_RFID_KNOWN);
		} else if(structag.type==StructuredTag::EMPTY) {
			currentID = "";
			isNewID = true;
			dispatchEventSM(EVENT_RFID_UNKNOWN);
		}
	}


	String getPayload(String content){
		String payload=AsyncHttpRequest::getPayload(content);
		if(payload.indexOf("html")>-1) {
			if(payload.indexOf("NOTOK")>-1) payload="NOTOK";
			else if(payload.indexOf("OK")>-1) payload="OK";
		}
		Serial.println(String()+"getPayload final content: "+ payload);
		return payload;
	}

	void fetchSetId(String id){
		String requrl=String()+baseUrl+"set?id="+id+"&nohtml=1";
		requestbusy=true;
		//		if(vbeep) vbeep->playSpecial("reconnu");
		//		if(rgbled) rgbled->playColor(LED_ID_FOUND_COLOR);
		AsyncHttpRequest::fetch(
				requrl,
				[this](const StreamString *content, AsyncHttpRequest *req) {
			Serial.println(String()+"fetchSet success for url:"+req->getUrl());
			String payload=getPayload(content->c_str());

			if(payload=="OK") {
				idset=true;
				dispatchEventSM(EVENT_SET_SUCCESS);
			} else {
				dispatchEventSM(EVENT_SET_FAIL);
				idset=false;
			}
			knownid=true;
			requestbusy=false;
			//			Serial.println(String()+"Superscale is "+payload);
		},
		[this](const String &error, AsyncHttpRequest *req) {
			Serial.println(String()+"fetchSet failure for url:"+req->getUrl());
			Serial.printf("Reason: %s\n", error.c_str());
			//superscaleNotFound();
			dispatchEventSM(EVENT_SYNC_NOSUPERSCALE);
			requestbusy=false;
		}
		);
	}


	void fetchSetCommand(String command){
		String requrl=String()+baseUrl+"set?command="+command+"&nohtml=1";
		requestbusy=true;
		//		if(vbeep) vbeep->playSpecial("reconnu2");
		//		if(rgbled) rgbled->playColor(LED_COMMAND_FOUND_COLOR);
		AsyncHttpRequest::fetch(
				requrl,
				[this](const StreamString *content, AsyncHttpRequest *req) {
			Serial.println(String()+"DataManager::fetchSet success for url:"+req->getUrl());
			String payload=getPayload(content->c_str());
			if(payload.length()==0) {
				dispatchEventSM(EVENT_SET_COMMAND_FAIL);
			} else {
				dispatchEventSM(EVENT_SET_COMMAND_SUCCESS);
			}
			requestbusy=false;
		},
		[this](const String &error, AsyncHttpRequest *req) {
			Serial.println(String()+"fetchSet failure for url:"+req->getUrl());
			Serial.printf("Reason: %s\n", error.c_str());
			//superscaleNotFound();
			dispatchEventSM(EVENT_SYNC_NOSUPERSCALE);
			requestbusy=false;
		}
		);
	}

	String parseJsonCommand(String json){
		int idx=json.indexOf(COMMAND_KEYWORD);
		if(idx<0) return "";
		//		Serial.println(String()+"parseJsonCommand 1:"+json.substring(idx,json.length()));
		idx+=String(COMMAND_KEYWORD).length()+1;
		int idx2=json.indexOf("\"",idx);
		if(idx2<0) return "";
		//		Serial.println(String()+"parseJsonCommand 2:"+json.substring(idx2,json.length()));
		int idx3=json.indexOf("\"",idx2+1);
		if(idx3<0) return "";
		//		Serial.println(String()+"parseJsonCommand 3:"+json.substring(idx3,json.length()));
		return json.substring(idx2+1,idx3);
	}

	void fetchCommand(){
		String requrl=String()+baseUrl+"json";
		requestbusy=true;
		//		if(vbeep) vbeep->playSpecial("reconnu3");
		//		if(rgbled) rgbled->playColor(LED_COMMAND_FOUND_COLOR);
		AsyncHttpRequest::fetch(
				requrl,
				[this](const StreamString *content, AsyncHttpRequest *req) {
			Serial.println(String()+"fetchCommand success for url:"+req->getUrl());
			String payload=AsyncHttpRequest::getPayload(content->c_str());
			currentCommand=parseJsonCommand(payload);
			Serial.println(String()+"fetchCommand got command:'"+currentCommand+"'");
			if(currentCommand.length()>0) {
				dispatchEventSM(EVENT_GET_COMMAND_SUCCESS);
			} else dispatchEventSM(EVENT_GET_COMMAND_FAIL);
			requestbusy=false;
			//Serial.println(String()+"Superscale is "+payload);
		},
		[this](const String &error, AsyncHttpRequest *req) {
			Serial.println(String()+"fetchSet failure for url:"+req->getUrl());
			Serial.printf("Reason: %s\n", error.c_str());
			//superscaleNotFound();
			dispatchEventSM(EVENT_SYNC_NOSUPERSCALE);
			requestbusy=false;
		}
		);
	}


	void fetchGenerate(){
		String requrl=String()+baseUrl+"generateID?nohtml=1";
		requestbusy=true;
		AsyncHttpRequest::fetch(
				requrl,
				[this](const StreamString *content, AsyncHttpRequest *req) {
			Serial.println(String()+"fetchGenerate success for url :"+req->getUrl());
			String payload=getPayload(content->c_str());
			Serial.println(String()+"Content: "+ payload);
			if(payload!="NOTOK") {
				if(currentID!=payload) {				// updating ID
					Serial.println(String()+"DataManager:fetchGenerate payload: "+payload);
					if(payload!="OK") currentID=payload;
				}
				idset=true;
				dispatchEventSM(EVENT_GENERATE_SUCCESS);
				if(payload!="OK") currentID=payload;
			} else {
				dispatchEventSM(EVENT_GENERATE_FAIL);
				idset=false;
				currentID="";
				currentTag="";
			}
			newid=true;
			requestbusy=false;
		},
		[this](const String &error, AsyncHttpRequest *req) {
			Serial.println(String()+"fetchGenerate failure for url"+req->getUrl());
			Serial.printf("Reason: %s\n", error.c_str());
			//superscaleNotFound();
			dispatchEventSM(EVENT_SYNC_NOSUPERSCALE);
			requestbusy=false;
		}
		);
	}







	void fetchSave(String url){
		String requrl=url;
		requestbusy=true;
		AsyncHttpRequest::fetch(
				requrl,
				[this](const StreamString *content, AsyncHttpRequest *req) {
			Serial.println(String()+"DataManager::fetchSave success for url "+req->getUrl());
			//			Serial.printf("\n--- SUCCESS ---\n%s\n", content->c_str());
			String payload=getPayload(content->c_str());
			if(payload!="NOTOK") {
				if(currentID!=payload) {				// updating ID
					Serial.println(String()+"DataManager:fetchSave payload: "+payload);
					if(payload!="OK") {
						currentID=payload;
						//rfiddb.set(currentTag, currentID);
						if(isNewID) isNewID=false;
					}
				}
				idset=true;
				saved=true;
				dispatchEventSM(EVENT_SAVE_SUCCESS);
				if(isNewID) {
					//rfiddb.set(currentTag, currentID);
					isNewID = false;
				}
			} else {
				idset=false;
				dispatchEventSM(EVENT_SAVE_FAIL);
			}
			Serial.println(String()+"Superscale is "+payload);
			//			if(vbeep) vbeep->playSpecial("beep");
			//			if(rgbled) rgbled->playColor(LED_BUSY_COLOR);
			requestbusy=false;
		},
		[this](const String &error, AsyncHttpRequest *req) {
			Serial.println(String()+"AsyncHttpRequest::fetch failure for url"+req->getUrl());
			Serial.printf("\n--- FAILED ---\nReason: %s\n", error.c_str());
			requestbusy=false;
			dispatchEventSM(EVENT_SYNC_NOSUPERSCALE);
		}
		);

	}



	void triggerSave() {
		if(saved) {
			Serial.println("Already saved this one.");
			dispatchEventSM(EVENT_SAVE_FAIL);
			return;
		}
		if(requestbusy) {
			Serial.println("Request is busy.");
			dispatchEventSM(EVENT_SAVE_FAIL);
			return;
		}

		Serial.println(String()+"triggerSave currentID:"+currentID);
		if (currentID.length() == 0) {
			Serial.println("Nothing to save!");
			dispatchEventSM(EVENT_SAVE_FAIL);
			return;
		}

		String saveUrl = baseUrl+"save";

		// Branch logic based on whether we know this RFID or not
		if (isNewID) {// It's new: Remote save requires the ID in the URL
			String urldate=getConfig("date");
			if(urldate.length()==0) {
				Serial.println("triggerSave: no urldate in config file (ie 'blockdate' or something else)");
				dispatchEventSM(EVENT_SAVE_FAIL);
				return;
			}
			saveUrl=saveUrl+"?id="+currentID+"&command="+urldate+"&argument=TODAY()&nohtml=1";
			Serial.println("Saving NEW ID Remotely: " + saveUrl);

			// 2. Local Save: add to DB and update the JSON file

		} else {// It's known: Remote save just calls /save
			Serial.println("Saving KNOWN ID Remotely: " + saveUrl);
		}
		notifyNext=true;
		fetchSave(saveUrl);
		//httpClient.GET(saveUrl);

	}


	void update(){
		/*	httpClient.update();
		if (httpClient.isFinished()) {
			if (httpClient.isSuccess()) {
				// Extract the payload dynamically
				String data = httpClient.getPayload();
				Serial.println("Data received: " + data);

				// Trigger your buzzer / ID loaders here
				urlLoaded(data);
				//vbeep->playSpecial("agree");
			} else {
				Serial.println("Request Failed!");
				if(vbeep) vbeep->playSpecial("doublefail");
			}
			// 3. Clear the state so this block doesn't keep running continuously
			httpClient.resetState();
		}
		 */
	}
private:
	// --- Function to load config.json into the map ---
	bool loadConfig(String filename) {
		Serial.println("Loading config file: " + filename);
		DynamicJsonDocument doc = loadJSON(filename);
		JsonObject root = doc.as<JsonObject>();
		if (root.isNull() || root.size() == 0) {
			Serial.println("Config file is empty or invalid JSON.");
			return false;
		}
		bool changed = false;
		for (JsonPair kv : root) {
			String key = kv.key().c_str();
			String val = kv.value().as<String>();// Using .as<String>() safely converts ints/floats/booleans into Strings if needed
			configMap[key] = val;
			changed = true;
			Serial.println(String()+"Config file > " + key + ": " + val);
		}
		return changed;
	}
	/*

	void errorAV(){
		if(vbeep) vbeep->playSpecial("shortdown");
		if(rgbled) rgbled->playColor(LED_ERROR_COLOR);
	}
	void bigErrorAV(){
		if(vbeep) vbeep->playSpecial("fail");
		if(rgbled) rgbled->playColor(LED_BIG_ERROR_COLOR);
	}
	 */

};



#endif
