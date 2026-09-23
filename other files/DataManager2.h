#ifndef DATAMANAGER_H
#define DATAMANAGER_H

#include <mylib.h>    // Arduino IDE don't want to take subfolders without including a file at the root
#include <datastruct/DataMap.h>
#include <datastruct/JSON.h>
#include <fs/SimpleFS.h>

#include "AsyncTCP.h"

#include "ConfigManager.h"

#define TCP_PORT 80

#define HISTORYFILENAME "history.json"
#define IDMAP_FILENAME "idmap.json"

#define DEFAULT_SERVER_URL "http://192.168.1.102/"
#define DEFAULT_SETID_URL "set?id="
#define DEFAULT_GETID_URL "get?id"

/*
	We can imagine many commands :
		- ASSOCIATE : scan before virgin or non virgin rfid, in that case the tag is not loadeed to superscale
		- associate FROMTO : scan once before and once after a series of rfid
		- STORE set for later into another rfid (how to name the set ? or at least species, use "name" command on superscale) & associate set from this rfid
			- scan save set once before and once after a series of rfid, then scan the storage rfid
			- scanning the storage rfid will recall the set, scanning associate fromto will associate the set,
				- should we use fromto, storage, fromto or just storage, fromto ?
		- sets are stored in a special file
		- CLEAR to clear rfid(s) : one by one or before after
		-

*/

DataMap Idmap;

DataMap parseFile(String filecontent) {
	return DataMap();
}

void loadHistory(String historyfilename){
	String filecontent=SimpleFS.readFileToString(historyfilename);
	filecontent=removeComments(filecontent);
	//	Serial.println(String()+"preparse file "+HISTORYFILENAME+" content :"+filecontent);
	//	if(filecontent.size()==0) return;
	DataMap datamap=parseFile(filecontent);
	//	Serial.println(String()+"postparse file "+HISTORYFILENAME+" content :");
	datamap.printMap();
}
void loadIdMap(String idmapfilename){
	String filecontent=SimpleFS.readFileToString(idmapfilename);
	filecontent=removeComments(filecontent);
	//	Serial.println(String()+"preparse file "+IDMAPFILENAME+" content :");
	Idmap=parseFile(filecontent);
	Serial.println(String()+"file "+IDMAP_FILENAME+" content :");
	Idmap.printMap();
	//	Serial.println(String()+"postprint file "+IDMAPFILENAME+" content :");
};




void onConnect(void* arg, AsyncClient* client) {
	Serial.printf("\n client has been connected to on port %d \n", TCP_PORT);
}

class TagIdMap {// option 1 load at start in mem and use this map, option 2 read the file every time to find an id
	String idmapfilename;
public:
void init(String idmapfilename1){
	idmapfilename=idmapfilename1;
	loadIdMap(idmapfilename);
}

};

#define IDMAP_FIELD "idmap-filename"
#define ASSOCIATE_TAG_FIELD "associate-tag"



String getIfExists(DataMap *configmap, String key, String defaultvalue="", bool setifnotfound=false){
	if(configmap->exists(key)) return configmap->get(key);
	if(setifnotfound) configmap->update(key,defaultvalue);
	return defaultvalue;
}

class DataManager {
	String idmapfilename=IDMAP_FILENAME;
	String waitingtag, lasttag, lastid;
	String associatetag;
	bool connected=false, hassavetag=false;
	TagIdMap idmapman;
	ConfigManager configman;
	DataMap *configmap=0;

	String serverurl=DEFAULT_SERVER_URL, seturl=DEFAULT_SETID_URL, geturl=DEFAULT_GETID_URL;
	unsigned tcpport=TCP_PORT;
	AsyncClient* currentclient=0;

	Enum Modes {Ready, Associate} currentmode;

public:
	DataManager(){}
	void init(String configfilename){
		configman.load(configfilename);
		configmap=configman.getMap();
		if(!configmap) return;
		idmapfilename=getIfExist(IDMAP_FIELD,IDMAP_FILENAME,1);
		associatetag=getIfExist(ASSOCIATE_TAG_FIELD);
//		if(configmap->exists(IDMAP_FILENAME)) idmapfilename=configmap->get(IDMAP_FILENAME);
//		if(configmap->exists(ASSOCIATE_TAG)) associatetag=configmap->get(ASSOCIATE_TAG);
		idmapman.init(idmapfilename);
	}
	void setVBeeper(VBeeper *beeper){vbeeper=beeper;}
	void connectedNetwork(){
		connected=true;
		if(waitingtag.length()>0){
			processTagNetwork();
		}
	}

	void processTag(String tag1){	// TODO : write in tag history file
		vbeeper->playSpecial("whisper");
		waitingtag=tag1;
		lastid=idmapman.find(waitingtag);

		if(!hassavetag) {
			associatetag=waitingtag;
			hassavetag=true;
			waitingtag="";
			configmap->update(ASSOCIATE_TAG_FIELD,associatetag);
			configman.save();
			return;
		}// if no prior data, first tag read is an associate tag

		if(waitingtag==associatetag){
			currentmode=Associate;
		}

		if(connected) processTagNetwork();
	}

	void processTagNetwork(){
		if(currentmode==Associate) {
			String id=remoteGetID();
			associate(waitingtag,id);
			return;
		}
		if(lastid.length()>0) remoteSetID(); // make a url request to set id on superscale
		lasttag=waitingtag;
		waitingtag="";
	}

	void remoteSetID(){
		String url=serverurl+seturl+lastid;
		if(!currentclient) delete currentclient;
		currentclient = new AsyncClient;
		currentclient->onData(&handleData, this);
		currentclient->onConnect(&onConnect, this);
		currentclient->connect(serverurl, tcpport);
	};

	void processHTTPData(uint8_t* data, unsigned len){
		// check we have an ok
		Serial.printf("\n data received from %s \n", currentclient->remoteIP().toString().c_str());
		Serial.write((uint8_t*)data, len);
	}

	/* event callbacks */
	static void handleData(void* obj, AsyncClient* client, void *data, size_t len) {
		obj->processHTTPData((uint8_t*)data, len);

	}

	void associate(String tag, String id){
		// save to file, or at least to memory
	}
};



///////////////////////////////////////////////////
class ConfigJSONLoader: public HeavyJSONLoader {
	DataMap *configmap;
public:
	ConfigJSONLoader (DataMap *configmap0, std::string *json, bool cleanjson0=false, std::string *err0=0):
		HeavyJSONLoader(json,cleanjson0,err0), configmap(configmap0){};
	virtual bool valueCallBack(){
		String path=getPath().c_str(), key=stringKey().c_str();
		if(path=="/" && key[0]=='/') path="";
		String fullpath=path+key;
		GenString val=stringValue();
		Serial.println(String()+"ConfigJSONLoader:"+fullpath.c_str() + ":" + val.c_str() );
		configmap->update(fullpath.c_str(),val.c_str());
		return JSONLoader::valueCallBack();
	};
};
auto parse(String filecontent){
	std::string filestr=filecontent.c_str();
	DataMap datamap;
	ConfigJSONLoader heavyloader(&datamap, &filestr);	//why do we need to give the json at loader creation ?
	parseJSON(filestr, &heavyloader);
	return datamap;
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#endif
