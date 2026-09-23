#ifndef SIMPLEFSESP32_H
#define SIMPLEFSESP32_H

#include <string>
/*
  SimpleFS Status : mostly code salvaged from ioktopus and some new using String rather than std::string. Most likely will be moved to littlefs, hopefully with only few defines.

 * */
#ifdef ESP8266BUILD
#undef FTEMPLATE
#define FFX(string_literal) (reinterpret_cast<const __FlashStringHelper *>(((__extension__({static const char __c[] __attribute__((section(FTEMPLATE))) = ((string_literal)); &__c[0];})))))
#define RF(x) String(FFX(x)).c_str()
#define FTEMPLATE ".irom.text.rf"
#else
#define RF(x) x
#endif



void println(std::string str){
	String str2=String(str.c_str());
	Serial.println(str2);
}
void print(std::string str){
	String str2=String(str.c_str());
	Serial.print(str2);
}


#include <FS.h>
#include <vector>

// Define ESP32BUILD for ESP32 platform
#ifndef ESP32BUILD
#define ESP32BUILD
#endif

#ifdef ESP32BUILD
	#include <LittleFS.h>
	#define CurFS LittleFS

	#if CurFS==LITTLEFS
	#include <LITTLEFS.h>
	#endif

	#if CurFS==LittleFS
	#include <LittleFS.h>
	#endif

	#if CurFS==SPIFFS
	#include <SPIFFS.h>
#endif

#endif

#ifdef ESP8266BUILD
	#define CurFS LittleFS

	#if CurFS==LittleFS
	#include <LittleFS.h>
	#endif

#endif

struct FileFS {
	std::string name;
	unsigned int size;
	const char* getType(){return "FileFS";};
};

struct SimpleFileFS : public FileFS
{
	File fileptr;
	SimpleFileFS(std::string name0, unsigned int size0, File fileptr0): fileptr(fileptr0) {name=name0; size=size0;};
	const char* getType(){return "SimpleFileFS";}
};


class SimpleFSEsp32 {
	long totalbytes=-1;
	long usedbytes=-1;
public:

	SimpleFSEsp32(){}
	void begin(){CurFS.begin();};
	bool exists(std::string path){return CurFS.exists(path.c_str());};
	bool exists(String path){return CurFS.exists(path.c_str());};
	bool exists(const char *path){return CurFS.exists(path);};
	bool rename(String namesrc,String namedst){		//	Serial.println(String()+"SimpleFS renaming "+namesrc+" "+namedst);
		return CurFS.rename(namesrc, namedst);
	}
	File getFilePtr(FileFS *ffs){
		if(ffs->getType()=="SimpleFileFs") return ((SimpleFileFS*) ffs)->fileptr;
		return File();
	}


	unsigned long fileSize(std::string path){
		File f = CurFS.open(path.c_str(), "r");
		if (!f) { println(std::string()+RF("CurFS::fileSize Error: could not open file ")+path.c_str()); return 0; }
		unsigned long sz=f.size();
		f.close();
		return sz;
	};
	// code triggering C9000 warning

	/*
	void printFiles(){
		// list all files
		String str;
		Dir dir = CurFS.openDir(String("/"));
		while (dir.next()) {
			str += dir.fileName();
			str +=  "(";
			str += dir.fileSize();
			str += "bytes)\r\n";
		}
		Serial.println(str);
	};
	 */
	unsigned int getTotalSpace(){
		if(totalbytes>=0) return totalbytes;
#ifdef ESP32BUILD
		totalbytes =CurFS.totalBytes();
		return totalbytes;
#endif
#ifdef ESP8266BUILD
		FSInfo fs_info;
		CurFS.info(fs_info);
		totalbytes =fs_info.totalBytes;
		return totalbytes ;
#endif
	}
	unsigned int getUsedSpace(){//	Serial.println("Calling SimpleFSEsp32.getUsedSpace ! should not happen too often");
		if(usedbytes>=0) return usedbytes;
#ifdef ESP32BUILD
		usedbytes= CurFS.usedBytes();
		return usedbytes;
#endif
#ifdef ESP8266BUILD
		FSInfo fs_info;
		CurFS.info(fs_info);
		usedbytes= fs_info.usedBytes;
		return usedbytes;
#endif
	}
/*
	unsigned int getTotalSpace(){// on ESP32, calling LITTLEFS.totalBytes() or LITTLEFS.usedBytes() too often create "corrupt dir pair" error when reading elsewhere
		if(totalbytes<0) totalbytes=CurFS.totalBytes();
		return totalbytes;
	}
	unsigned int getUsedSpace(){return getFolderSize();}
*/
	unsigned int getFreeSpace(){return (getTotalSpace()-getUsedSpace());}



	long getFolderSize(String path="/"){
			File root = CurFS.open(path.c_str(),"r");

			if(!root){
				Serial.println(String()+"- failed to open directory "+path);
				return -1;
			}
			if(!root.isDirectory()){
				Serial.println(String()+" - not a directory "+path);
				return -1;
			}
			long size=0;
			File file = root.openNextFile();

			while(file){
				if(file.isDirectory()){getFolderSize(file.name());}
				else {
					size+=file.size();
					Serial.println(String()+"getFolderSize: adding file "+file.name()+" size "+file.size()+" to "+size);
				}
				file = root.openNextFile();
			}
		}


	void printInfo(){
		Serial.println("File system on board : LittleFS");
		unsigned long gt=getTotalSpace(),us=getUsedSpace(),as=gt-us;
		float peru=10000.0*us/gt, perf=10000.0*as/gt;
		Serial.println(String()+"- total space : "+gt+" byte");
		Serial.println(String()+"- used space : "+us+" byte "+"("+peru/100.0+"%)");
		Serial.println(String()+"- available space : "+as+" byte ("+ (perf/100.0)+"%)");
		Serial.println(String()+"Files : ");
		printFiles();
	}

	void printFiles(String path="/"){
/*		 Dir dir = SPIFFS.openDir("/data");
	  // or Dir dir = LittleFS.openDir("/data");
		 while (dir.next()) {
         Serial.print(dir.fileName());
         if(dir.fileSize()) {
         File f = dir.openFile("r");
         Serial.println(f.size());
    }
}*/
//		Serial.println(String()+"printFiles:"+path);
		File root = CurFS.open(path.c_str(),"r");
		if(!root){
			Serial.println(String()+"- failed to open directory "+path);
			return;
		}
		if(!root.isDirectory()){
			Serial.println(String()+" - not a directory "+path);
			return;
		}
		String str;
		File file = root.openNextFile();

		while(file){
			if(file.isDirectory()){
				//str+=String()+file.name()+" (dir)\r\n";
				Serial.println(String()+file.name()+" (dir)");
	//			Serial.println(String()+"printFiles path:"+path+" filename:"+file.name()+" (dir)");
				printFiles(file.name());//maybe we shouldnt recurse to print all files of all directories ?
			} else {
				//str+=String()+file.name()+" ("+file.size()+"bytes)\r\n";
				Serial.println(String()+file.name()+" ("+file.size()+" bytes)");
			}
			file = root.openNextFile();

		}
	}


	bool erase(const char *path){return erase(std::string(path));}
	bool erase(String path){
		//	Serial.println(String()+"SimpleFS: erasing file "+path);
		return erase(std::string(path.c_str()));}
	bool erase(std::string path) {
#ifndef CurFSNOWRITE
		if(CurFS.remove(path.c_str())) {println(std::string() + RF("Deleted with success file ")+path.c_str());usedbytes=-1;return true;}
		else {println(std::string() + RF("Error deleting file ")+path);return false;}
#else
		return false;
#endif
	};

	unsigned int appendToFile(const char * path, const char * txt, unsigned size) {
#ifndef CurFSNOWRITE
		File f = CurFS.open(path, FILE_APPEND);
 		if (!f) {println(std::string()+("CURFS::appendToFile: Could not append to file ")+path);return 0;}
		//	println(std::string()+RF("CURFS::appendToFile: appending ")+path);
 		usedbytes=-1;
		unsigned int w=f.write((const unsigned char*)txt, size);
 		f.close();
 		return w;
#else
		return 0;
#endif
	}
	unsigned int appendToFile(String path, String txt) {
//		Serial.println(String()+"SimpleFSEsp32::appendToFile"+path);
		return appendToFile(path.c_str(), txt.c_str(), txt.length());
	}
	unsigned int appendToFile(std::string path, std::vector<unsigned char> vect) {
		return appendToFile(path.c_str(), (const char *)vect.data(), vect.size());

	}







	void eraseAllFiles(String path="/"){
		File root = CurFS.open(path.c_str(),"r");
		String str;
		File file = root.openNextFile();
		while(file){
			str+=path+file.name();
			str += "\n";
			file = root.openNextFile();
		}
		// list all files


		//Serial.println(str);
		// erase the list
		unsigned i=str.indexOf("\n"),ip=0;
		while (i<str.length()){
			String filename=str.substring(ip,i);
			erase(filename.c_str());
			Serial.println(RF("Erasing file :")+filename);
			ip=i+1;
			i=str.indexOf("\n",i+1);
		}
		usedbytes=-1;
	};


	///////////////////////////////
	std::vector<FileFS> listFiles(std::string path0, std::string filenamebase){	//should be recursive ?
		if(path0.substr(path0.length()-1)!="/") path0+="/";
		File root = CurFS.open(path0.c_str(),"r");

		std::vector<FileFS> v;
		filenamebase=path0+filenamebase;
		File file = root.openNextFile();
		while(file){
			std::string fname=file.name();
			//print(fname+" / "+String(dir.fileSize()).c_str());
			if(fname.substr(0,filenamebase.size())==filenamebase) {
				FileFS ffs;
				ffs.name=fname;
				ffs.size=file.size();
				v.push_back(ffs);
				//	Serial.print(RF(" added "));
				//	Serial.print(ffs.name.c_str());
			}
			//Serial.println();

		}
		return v;

	};
	/*

	std::vector<FileFS> listFilesByExt(std::string path0, std::string fileext){
		if(path0.substr(path0.length()-1)!="/") path0+="/";
		Dir dir = CurFS.openDir(path0.c_str());
		std::vector<FileFS> v;

		while (dir.next()) {
			std::string fname=dir.fileName().c_str();
			print(fname+" / "+String(dir.fileSize()).c_str());
			if(fname.size()>fileext.size() && fname.substr(fname.size()-fileext.size())==fileext) {
				FileFS ffs;
				ffs.name=fname;
				ffs.size=dir.fileSize();
				v.push_back(ffs);
				Serial.print(RF(" added "));
				Serial.print(ffs.name.c_str());
			}
			Serial.println();

		}
		return v;
		return std::vector<FileFS>();};


	 */



	unsigned int rewriteFile(const char *path, const char *buff, unsigned int size) {
#ifndef CurFSNOWRITE
		File f = CurFS.open(path, "w");
		if (!f) {println(std::string()+RF("CURFS::rewriteFile: Could not rewriteFile ")+path);return 0;}
	//	println(std::string()+RF("CURFS::rewriteFile: rewriting ")+path);

		unsigned int w=f.write((unsigned char *)buff, size);
		usedbytes=-1;
		f.close();
		return w;
#else
		return 0;
#endif
	}
	unsigned int rewriteFile(std::string path, unsigned char *buff, unsigned int size){
#ifndef CurFSNOWRITE
		File f = CurFS.open(path.c_str(), "w");
		if (!f) {println(std::string()+RF("CURFS::rewriteFile: Could not rewriteFile ")+path);return 0;}
		println(std::string()+RF("CURFS::rewriteFile: rewriting ")+path);

		unsigned int w=f.write(buff, size);
		usedbytes=-1;
		f.close();
		return w;
#else
		return 0;
#endif
	};


	unsigned int rewriteFile(std::string path, std::vector<unsigned char> vect) {
		return rewriteFile(path,vect.data(),vect.size());
	}



	FileFS* openFile(std::string path,std::string mode="r"){
//		Serial.println(String()+"opening file"+path.c_str());
		File f = CurFS.open(path.c_str(), mode.c_str());
		if (!f) return 0;

		return new SimpleFileFS(path,f.size(),f);
	};

	unsigned int rewriteFile(String path, String content){
		return rewriteFile(path.c_str(),(unsigned char*)content.c_str(),content.length());
	}

	// convenience overload to accept Arduino String directly
	unsigned int rewriteFile(const char *path, String content){
		return rewriteFile(path, (const char*)content.c_str(), content.length());
	}

	void closeFile(FileFS* ffs){
		SimpleFileFS *cffs=(SimpleFileFS*) ffs;
		cffs->fileptr.close();
		delete cffs;
	};

	unsigned long readFile(unsigned char *r,unsigned long size,FileFS* ffs){
		SimpleFileFS *cffs=(SimpleFileFS*) ffs;
		//		long pos = ftell(cffs->fileptr);
		//		println("readFile: reading at pos:"+to_string(pos));
		size_t res= cffs->fileptr.readBytes((char *)r, size);
		//		pos = ftell(cffs->fileptr);
		//		println("readFile: post reading at pos:"+to_string(pos));
		return res;
	};

	void seekFile(unsigned long pos, FileFS* ffs){
		if(!ffs) return;
		SimpleFileFS *cffs=(SimpleFileFS*) ffs;
		cffs->fileptr.seek(pos,SeekSet);
	};

	String readLine(FileFS *ffs) {return readStringUntil(ffs,'\n');}

	String readStringUntil(FileFS *ffs, char c){
		if(!ffs) return "";
		SimpleFileFS *cffs=(SimpleFileFS*) ffs;
//		Serial.println(String()+"SimpleFSEsp32::readStringUntil : '"+c+"' "+(unsigned int)cffs);

		String s= cffs->fileptr.readStringUntil(c);
//		Serial.println(String()+"SimpleFSEsp32::readStringUntil string: '"+s+"'");
		return s;
	}

	unsigned char* readFileBuffer(std::string path,size_t &size){
		File f = CurFS.open(path.c_str(), "r");
		if (!f) { println(std::string()+RF("readFileBuffer Error: could not open file ")+path.c_str()+"\n"); return 0; }
		unsigned char * buffer = new unsigned char[f.size()];
		size= f.readBytes((char *)buffer, f.size());
		//println(std::string()+ RF("reading ")+to_string(size));
		f.close();
		return buffer;
	};
	String readFileToString(String path){
		File f = CurFS.open(path.c_str(), "r");
		if (!f) { Serial.println(String()+"readFileToString Error: could not open file '"+path+"'\n"); return String(); }
		unsigned char * buffer = new unsigned char[f.size()+1];
		//size_t size=
				f.readBytes((char *)buffer, f.size());
		*(buffer+f.size())=0;				//		Serial.println(String()+"readFileToString f.size():'"+f.size()+"'\n");//		Serial.println(String()+"readFileToString size:'"+size+"'\n");
		f.close();
		String str((const char *)buffer);
		delete buffer;				//		Serial.println(String()+"readFileToString 3 str:'"+str+"'\n");
		return str;
	}
	std::string readFileToStdString(std::string path,size_t &size){
		File f = CurFS.open(path.c_str(), "r");
		if (!f) { println(std::string()+RF("readFileToString Error: could not open file '")+path.c_str()+"'\n"); return std::string(); }
		unsigned char * buffer = new unsigned char[f.size()+1];
		size= f.readBytes((char *)buffer, f.size());
		*(buffer+f.size())=0;
		//		println(std::string()+ RF("reading ")+to_string(size));
		f.close();
		std::string str((const char *)buffer);
		delete buffer;
		return str;
	};

	void printFile(std::string path,size_t &size, int blocksize=128){
		File f = CurFS.open(path.c_str(), "r");
		if (!f) { println(std::string()+RF("printFile Error: could not open file ")+path.c_str()+"\n"); return ; }
		int sz=f.size();
		size=0;
		unsigned char * buffer = new unsigned char[blocksize+1];
		while(sz>0){
			if(sz<blocksize) blocksize=sz;
			size= f.readBytes((char *)buffer, blocksize);
			buffer[blocksize]=0;
			Serial.print(String((char *)buffer));
			sz-=blocksize;
		}
		f.close();
		return;
	};

} SIMPLEFSESP32;
//684 static mem, i guess its CurFS.begin()



#endif
