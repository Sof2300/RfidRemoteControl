#ifndef CYCLEMANAGER_H
#define CYCLEMANAGER_H

#include <TimeUtil.h>

#define DEFAULT_RELAY_PIN 5

#define CYCLE_ON_DURATION_DEFAULT 60


#define CYCLE_DURATION_DEFAULT 3600


//#define CURRENT_TIME_STAMP "/data/current-timestamp"

#define CYCLE_DATAGET_URL "/set","x-Date", "/eraselog"


#define LOGFILE_NAME "logfile.txt"




void logMessage(String message, bool adddate=true) {
	String timestring="";
	if(adddate) timestring=getTimeString()+" - ";
	String fullmessage= timestring + message+"\n";
	SIMPLEFS.appendToFile(LOGFILE_NAME, fullmessage);
}
void eraseLogFile() {SIMPLEFS.erase(LOGFILE_NAME);}




class CycleActivator;
class CycleManager;

class CycleUpdater{
	CycleManager *manager;
public:
	CycleUpdater(CycleManager*cyclemanager): manager(cyclemanager){}
	void update();
};

void stopOn_stat(CycleActivator *);
void endCycle_stat(CycleActivator *);

class CycleActivator {
	unsigned int pin;
	uint32_t onduration=0,totalduration=0;

	Ticker onticker,totalticker;
	uint32_t startts=0;
	uint32_t nextstartts=0;
	uint32_t suspendts=0;
	uint32_t resumets=0;
	uint32_t zerotime=0;
	unsigned currentstate=0;
	unsigned running=0;

	CycleUpdater *updater=0;



	void setPinValue(unsigned value){
		digitalWrite(pin,value);
		currentstate=value;
		Serial.println(String()+"CycleManager digitalWrite pin "+pin+" with value :"+value);
	}
	void update(){if(updater!=0) updater->update();}

	uint32_t getNow(){	// correct time when clock is resynced	// redundant if called from cyclemanager when it happens
		correctZeroTime();
		return now32();
	}


public:
	void suspendCycle(){
		suspendts=now32();
		onticker.detach();
		totalticker.detach();
		running=0;
		update();
	}
	void resumeCycle(){
		if(running==1) return;

		uint32_t diff=suspendts-resumets;
		diff=diff%(totalduration);
		resumets=now32()-diff;	// theorical startts taking account the pause
		suspendts=0;

		if(diff<(onduration) && currentstate==0) {setPinValue(1);onticker.once_ms((onduration-diff)*1000,stopOn_stat,this);}
		if(diff>(onduration) && currentstate==1) {setPinValue(0);}
		totalticker.once_ms((totalduration-diff)*1000, endCycle_stat,this);
		nextstartts=now32()+totalduration-diff;
		running=1;
		update();
	}

	void stopOn() {setPinValue(0);currentstate=0;update();}
	void endCycle() {restartCycle();}

	void setUpdater(CycleUpdater *nupdater) {updater=nupdater;}

	void correctZeroTime(){	// correct time when clock is resynced
		uint32_t ztime=now32ZeroTime();
		if(zerotime==ztime) return;

		int64_t diff=0;
		diff=ztime-zerotime;
		zerotime=ztime;

		if(startts>0) startts+=diff;
		if(resumets>0) resumets+=diff;
		if(suspendts>0) suspendts+=diff;
		if(nextstartts>0) nextstartts+=diff;

	}
	void changeRelayPin(unsigned int relay) {
		if(relay==pin) return;
		setPinValue(0);
		pin=relay;
		setPinValue(currentstate);
	}
	unsigned int getPin(){return pin;};

public:
	CycleActivator(unsigned int relaypin) {pin=relaypin;

	}
	CycleActivator(unsigned int relaypin, uint32_t newonduration, uint32_t newtotalduration) {pin=relaypin;onduration=newonduration;totalduration=newtotalduration;}

	void setPin (unsigned int relaypin) {pin=relaypin;}
	void setCycleDurations (uint32_t newonduration, uint32_t newtotalduration) {
		onduration=newonduration;totalduration=newtotalduration;}

	void restartCycle(){
		//setPinValue(0);
		startts=now32();
		resumets=startts;
		onticker.once_ms(onduration*1000, stopOn_stat,this);
		totalticker.once_ms(totalduration*1000, endCycle_stat,this);
		nextstartts=startts+totalduration;
		setPinValue(1);
		running=1;
		update();
	}

	void adjustCycleDurations(uint32_t newonduration, uint32_t newtotalduration){// without restarting
		if(totalduration==newtotalduration && onduration==newonduration) return;
		uint64_t now=now32();
		uint64_t diff=now-startts;
		diff=diff%(newtotalduration);

		onduration=newonduration;
		totalduration=newtotalduration;

		if(diff<(newonduration) && currentstate==0) {setPinValue(1);onticker.once_ms((newonduration-diff)*1000,stopOn_stat,this);}
		if(diff>(newonduration) && currentstate==1) {setPinValue(0);onticker.detach();}
		totalticker.detach();
		totalticker.once_ms((totalduration-diff)*1000, endCycle_stat,this);
		nextstartts=now+totalduration-diff;
		update();
	}

	void setON(){setPinValue(1);suspendCycle();}
	void setOFF(){setPinValue(0);suspendCycle();}
	void switchState(){if(currentstate==1) setOFF();else setON();}
	void suspendOrResume(){if(running==1) suspendCycle();else resumeCycle();}

public:
	uint32_t getOnDuration(){return onduration;}
	uint32_t getCycleDuration(){return totalduration;}
	uint32_t getCurrentState(){return currentstate;}
	uint32_t getCycleState(){return running;}
	uint32_t getLastStartTS(){return startts;}
	uint32_t getNextStartTS(){return nextstartts;}
	uint32_t getZeroTime(){return zerotime;}

};

void stopOn_stat(CycleActivator *cycleman){cycleman->stopOn();}
void endCycle_stat(CycleActivator *cycleman){cycleman->endCycle();}


/////////////////////////////////////////////////////
class CycleManager : public EventListener{

	CycleActivator cycleactivator=CycleActivator(DEFAULT_RELAY_PIN);

	CycleUpdater updater=CycleUpdater(this);
	DataMap *datamap=0;
	uint64_t initstart=0;
	bool updating=false;

public:
	CycleManager (DataMap *datamap0): datamap(datamap0){ cycleactivator.setUpdater(&updater);}

	void init(){	// is the config file content in datamap ?
		Serial.println(String() + "CycleManager created CycleActivator with pin:"+cycleactivator.getPin());
		if(datamap->get(RELAY_PIN_NAMEPATH).length()!=0) cycleactivator.setPin(String(datamap->get(RELAY_PIN_NAMEPATH)).toInt());
		uint32_t cycle_on_duration=CYCLE_ON_DURATION_DEFAULT, cycle_duration=CYCLE_DURATION_DEFAULT;

		if(datamap->get(ON_DURATION_NAMEPATH).length()!=0)
			cycle_on_duration=(uint32_t) strToUint64(datamap->get(ON_DURATION_NAMEPATH).c_str());
		if(datamap->get(CYCLE_DURATION_NAMEPATH).length()!=0) cycle_duration=(uint32_t)strToUint64(datamap->get(CYCLE_DURATION_NAMEPATH).c_str());
		cycleactivator.setCycleDurations(cycle_on_duration, cycle_duration);

		cycleactivator.restartCycle();
		initstart=now32();
//		updateMap(TIMESTAMP_FIRST_START_NAMEPATH, String(tostring(initstart).c_str()));
		logMessage("",false);
		logMessage(String()+"CycleManager started first cycle with durations "+cycle_on_duration+"/"+cycle_duration);

	}

	bool updateMap(String path, String value){
		if(datamap==0) return false;
		if(datamap->exists(path) && datamap->get(path)==value) return false;
		datamap->update(path,value);
		return true;
	};

	void update(){// transfer cycledata into datamap
		if(updating) return;
		updateMap(ON_DURATION_NAMEPATH, String(cycleactivator.getOnDuration()));
		updateMap(CYCLE_DURATION_NAMEPATH, String(cycleactivator.getCycleDuration()));

		updateMap(SWITCH_STATE_NAMEPATH, String(cycleactivator.getCurrentState()));
		updateMap(CYCLE_STATE_NAMEPATH, String(cycleactivator.getCycleState()));

		updateMap(RELAY_PIN_NAMEPATH, String(cycleactivator.getPin()));

		updateMap(TIMESTAMP_LAST_CYCLE_START_NAMEPATH, String(tostring(cycleactivator.getLastStartTS()).c_str()));
		updateMap(TIMESTAMP_NEXT_CYCLE_START_NAMEPATH, String(tostring(cycleactivator.getNextStartTS()).c_str()));

	}

	std::vector<GenString> getUrlNames(){
		std::vector<GenString>vect={CYCLE_DATAGET_URL};
		return vect;
	}

	bool notify(GenString ename, Event *e){return notify(String(ename.c_str()),e);}
	bool notify(String ename, Event *e){
		if(ename[ename.length()-1]=='/') ename=ename.substring(0,ename.length()-1);
		StringMapEvent *se=0;
		if(e->isClassType("StringMapEvent")) se=(StringMapEvent *)e;
		if(se==0) return false;

		if(ename==String("/set")) {
			updating=true;
			bool changed=false, changetosave=false;
			String relaypin=se->get(RELAY_PIN_NAMEPATH);
			String on_duration=se->get(ON_DURATION_NAMEPATH);
			String total_duration=se->get(CYCLE_DURATION_NAMEPATH);

			String cyclestate=se->get(CYCLE_STATE_NAMEPATH);
			String switchstate=se->get(SWITCH_STATE_NAMEPATH);
			String laststart=se->get(TIMESTAMP_LAST_CYCLE_START_NAMEPATH);

			if(relaypin.length()>0 && isDigit(relaypin.c_str()) && (unsigned)relaypin.toInt()!=cycleactivator.getPin()) {
				cycleactivator.changeRelayPin(relaypin.toInt());
				changed=true;changetosave=true;
				logMessage(String()+"CycleManager changed relay pin to "+relaypin);
			}
			if(on_duration.length()>0 || total_duration.length()>0) {
				unsigned onduration, totalduration;
				if(on_duration.length()==0) onduration=cycleactivator.getOnDuration(); else onduration=on_duration.toInt();
				if(total_duration.length()==0) totalduration=cycleactivator.getCycleDuration(); else totalduration=total_duration.toInt();
				if(totalduration!=cycleactivator.getCycleDuration() || onduration!=cycleactivator.getOnDuration())
							{
							cycleactivator.adjustCycleDurations(onduration, totalduration);
							changed=true;changetosave=true;
							logMessage(String()+"CycleManager adjusted cycle durations to "+onduration+"/"+totalduration);
							}
			}

			if(switchstate.length()>0) {
				cycleactivator.switchState();
				changed=true;
				logMessage(String()+"CycleManager changed switch state to "+cycleactivator.getCurrentState());
			}
			if(cyclestate.length()>0) {
				cycleactivator.suspendOrResume();
				String srstate="suspend";
				if(cycleactivator.getCycleState()==1) srstate="resume";
				changed=true;
				logMessage(String()+"CycleManager changed suspend/resume state to "+srstate);
			}
			if(laststart.length()>0) {
				cycleactivator.restartCycle();
				changed=true;
				logMessage(String()+"CycleManager restarted cycle");
			}

			updating=false;
			if(changed) update();
			if(changetosave) saveDataMap(datamap);
			se->insertValue("response","ok");
			return true;
		}

		if(ename==String("/eraselog")) {
			eraseLogFile();
			se->insertValue("response","ok");
			return true;
		}

		if(ename==String("x-Date")) {
			// correct timestamp in cycle activator
		//	uint32_t zerotime=now32ZeroTime();
		//	int64_t diff=zerotime-cycleactivator.getZeroTime();
		 	cycleactivator.correctZeroTime();
			updateMap(TIMESTAMP_LAST_CYCLE_START_NAMEPATH, String(tostring(cycleactivator.getLastStartTS()).c_str()));
			updateMap(TIMESTAMP_NEXT_CYCLE_START_NAMEPATH, String(tostring(cycleactivator.getNextStartTS()).c_str()));
//			updateMap(TIMESTAMP_FIRST_START_NAMEPATH, String(tostring(initstart+diff).c_str()));
			logMessage(String()+"CycleManager synced clock to "+now32());
			se->insertValue("response","ok");
			return true;
		}
		return false;
	}





};

void CycleUpdater::update(){manager->update();}


#endif
