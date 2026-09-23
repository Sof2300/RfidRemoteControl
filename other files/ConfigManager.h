#ifndef CONFIGMANAGER_H
#define CONFIGMANAGER_H


#include <datastruct/DataMap.h>

#include <datastruct/JSON.h>



/*

class MyHeavyJSONLoader: public HeavyJSONLoader {
public:
	GenTreeMapHeavyJSONLoader (std::string *json, bool cleanjson0=false, std::string *err0=0):HeavyJSONLoader(json,cleanjson0,err0), map(map0){};
	virtual bool valueCallBack(){
		GenString path=getPath()+stringKey();
		std::cout << "MyHeavyJSONLoader:"<<path << ":" << stringValue() << std::endl;
		return JSONLoader::valueCallBack();
	};
};


class MyJSONLoader: public JSONLoader {
public:
	MyJSONLoader (std::string *json, bool cleanjson0=false, std::string *err0=0):JSONLoader(json,cleanjson0,err0){};
	virtual bool valueCallBack(){
		std::cout << "MyJSONLoader key :'"<<stringKey() << "', val:'"<<stringValue()<<"'"<< std::endl;
		return JSONLoader::valueCallBack();
	};
	virtual bool keyCallBack(){
		std::cout << "MyJSONLoader key :'"<<stringKey() << "', val:'"<<stringValue()<<"'"<< std::endl;
		return JSONLoader::keyCallBack();
	};
};


void main(){
	std::string jsoncontent="{\"XXX\":{\"hello\":\"123\"},\"bonjour\":\"321\"}";
	MyHeavyJSONLoader heavyloader(&jsoncontent);
 	MyJSONLoader loader(&jsoncontent);
	parseJSON(&loader);
	std::cout << "testIODatav03: executed 1" << std::endl;
	parseJSON(&heavyloader);
	std::cout << "testIODatav03: executed 2" << std::endl;
}
 */

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
//		Serial.println(String()+"ConfigJSONLoader:"+fullpath.c_str() + ":" + val.c_str() );
		configmap->update(fullpath.c_str(),val.c_str());
		return JSONLoader::valueCallBack();
	};
};

class ConfigManager {
	DataMap *configmap;
public:
	ConfigManager(DataMap * configmap0):configmap(configmap0){}
	DataMap* getMap() {return configmap;}

	void printMap(){
		Serial.println("Config loaded: ");
		for (auto it : *configmap->getMap()) {
			String key=it.first;
			Serial.println(String()+"- "+key+": "+it.second);
		}
	}
	void parse(String filecontent){
		std::string filestr=filecontent.c_str();
		ConfigJSONLoader heavyloader(configmap, &filestr);	//why do we need to give the json at loader creation ?
		parseJSON(filestr, &heavyloader);
	}

/*
	void parse(String filecontent){	// using ArduinoJSON.h
		//		String filecontent = "[\"one\",\"two\",{\"three\":3}]";
		DynamicJsonDocument doc2(2500);//filecontent.length());
		deserializeJson(doc2, filecontent);
 //		Serial.println(String()+"ConfigManager::parse0 :'"+filecontent+"'");
		JsonObject  root = doc2.as<JsonObject>();


		for (JsonPair kv : root) {
		    String key=kv.key().c_str();
//		    Serial.println(String()+"ConfigManager::parse1: key:"+key);
		    JsonObject obj =kv.value().as<JsonObject>();
		    for (JsonPair subv : obj) {
		    	String subkey=subv.key().c_str();
		    	String val=subv.value().as<char*>();
//		    	Serial.println(String()+"ConfigManager::parse2: val:"+val);
		    	configmap->update("/"+key+"/"+subkey,val);
		    }
		  //  Serial.println(kv.value().as<JsonArray>());
		}
//		Serial.println(String()+"ConfigManager::parse1 :'"+filecontent+"'");
/ *
		for (JsonVariant value : doc2) {		//		String val=value.as<char*>();		//		Serial.println(String()+"PriceManager::parse: "+val);
			Serial.println(String()+"JsonArray::parse+: "+value);
			JsonObject obj=value.as<JsonObject>();
			if(obj){
				String name=obj["title"];
				float price=obj["price"];
				Serial.println(String()+"JsonArray::parse2: name:"+name+" price:"+price);
				//			pricing.addPrice(name,price);
				JsonVariant var=obj["names"];
				for( JsonVariant kv : var.as<JsonArray>() ) {
					String val=kv.as<char*>();
					//				straintoname[val]=name;
					Serial.println(String()+"JsonArray::parse3: name:"+name+" val:"+val);
				}
			}
		}* /
		//	Serial.println(String()+"PriceManager::parse end :pricing:"+pricing.getPrices()->size());
	};*/

public:

	void load(){
		String filecontent=SimpleFS.readFileToString(CONFIGFILENAME);
		filecontent=removeComments(filecontent);
		parse(filecontent);
		printMap();
	}


};

#endif
