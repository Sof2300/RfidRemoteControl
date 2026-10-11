#ifndef RFIDSTATEMACHINE_H
#define RFIDSTATEMACHINE_H

#include <Ticker.h>
#include <queue>

#include <device/Beeper.H>
#include "RGBLed.h"
#include "DataManager.h"
#include "Definitions.h"
#include "StateMachine.h"


#define SYNC_WAIT_DURATION 5000

std::queue<String> evtQueue;
Ticker evtTicker;



class RfidStateMachine : public StateMachine {
protected:
	RGBLed *rgbled = nullptr;
	VBeeper *vbeeper = nullptr;
	DataManager *dataman = nullptr;

	Ticker smticker;

public:
	RfidStateMachine() : StateMachine("RfidStateMachine", nullptr, "WifiDisconnected") {}

	void sendTemporaryMessage(String message){
		if(!dataman) return;
			dataman->sendMessage(message,6);
	}
	void sendPermanentMessage(String message){
		if(!dataman) return;
			dataman->sendMessage(message,0);
	};


	void applyNetworkUI(State& s, const std::vector<String>& include = {}) {
		auto isIncluded = [&](String ev) {
			for (auto x : include) if (x == ev) return true;
			return false;
		};
		if (isIncluded(EVENT_WIFI_LOST))
			s.on(EVENT_WIFI_LOST, "WifiDisconnected").attachAction([this](EventSource& e) {
			vbeeper->playSpecial("fail");
			rgbled->playColor(LED_WIFI_FAILED_COLOR);
		});
		if (isIncluded(EVENT_SYNC_NOSUPERSCALE))
			s.on( EVENT_SYNC_NOSUPERSCALE, "SyncFailed").attachAction([this](EventSource& e) {
			vbeeper->playSpecial("fail");
			rgbled->playColor(LED_NO_SUPERSCALE_COLOR);
		});
		if (isIncluded(EVENT_SYNC_ERROR))
			s.on(EVENT_SYNC_ERROR, "WifiConnected").attachAction([this](EventSource& e) {
			vbeeper->playSpecial("shortfail");
			rgbled->playColor(LED_ERROR_COLOR);
		});
	}

	// --- WifiDisconnected ---
	State stateDisconnected{"WifiDisconnected", this, [this](State& s) {
		on("WifiDisconnected", EVENT_WIFI_CONNECTING, "WifiConnecting").attachAction([this](EventSource& e) {
			//			Serial.println("FSM: WifiDisconnected: WIFI_CONNECTING! Moving to WifiConnecting.");
			vbeeper->playSpecial("shortup");
			rgbled->playColor(LED_WIFI_CONNECTING_COLOR);
		});
	}};

	// --- WifiConnecting ---
	State stateConnecting{"WifiConnecting", this, [this](State& s) {
		on("WifiConnecting", EVENT_WIFI_CONNECTED, "WifiConnected").attachAction([this](EventSource& e) {
			//			Serial.println("FSM: WifiConnecting: EVENT_WIFI_CONNECTED! Moving to WifiConnected.");
			vbeeper->playSpecial("spring");
			rgbled->playColor(LED_WIFI_CONNECTED_COLOR);
		});
		on("WifiConnecting", EVENT_WIFI_ERROR, "WifiDisconnected").attachAction([this](EventSource& e) {
			//			Serial.println("FSM: WifiConnecting: EVENT_WIFI_ERROR! Moving back to WifiDisconnected.");
			vbeeper->playSpecial("fail");
			rgbled->playColor(LED_WIFI_FAILED_COLOR);
		});
		applyNetworkUI(s,{EVENT_WIFI_LOST});
	}};

	// --- WifiConnected ---
	State stateConnected{"WifiConnected", this, [this](State& s) {
		s.onEnter([this]() {
			dataman->wifiConnected();
		});
		applyNetworkUI(s,{EVENT_WIFI_LOST, EVENT_SYNC_ERROR, EVENT_SYNC_NOSUPERSCALE});
		on("WifiConnected", EVENT_SYNC_SUCCESS, "Synced").attachAction([this](EventSource& e) {
			//			Serial.println("FSM: WifiConnected: EVENT_SYNC_SUCCESS! Moving to Synced.");
			vbeeper->playSpecial("bungee");
			rgbled->playColor(LED_SYNCED_COLOR);
		});
	}};

	// --- WifiConnected ---
	State stateSyncFailed{"SyncFailed", this, [this](State& s) {
		s.onEnter([this]() {
			//			Serial.println(String()+"FSM: stateSyncFailed: Waiting for "+EVENT_SYNC_WAIT);
			smticker.once_ms(SYNC_WAIT_DURATION,[this]() {
				dispatchEventSM(EVENT_SYNC_WAIT);
			});
		});
		s.onLeave([this]() {
			smticker.detach();
		});
		applyNetworkUI(s,{EVENT_WIFI_LOST, EVENT_SYNC_ERROR, EVENT_SYNC_NOSUPERSCALE});
		on("SyncFailed", EVENT_SYNC_WAIT, "WifiConnected").attachAction([this](EventSource& e) {
			//			Serial.println("FSM: stateSyncFailed: EVENT_SYNC_WAIT! Moving to WifiConnected.");
		});
	}};

	// --- Synced ---
	class StateSynced: public StateMachine {
	public:
		RGBLed *rgbled = nullptr;
		VBeeper *vbeeper = nullptr;
		DataManager *dataman = nullptr;

		void sendTemporaryMessage(String message){
			if(!dataman) return;
			dataman->sendMessage(message,6);
		}
		void sendPermanentMessage(String message){
			if(!dataman) return;
			dataman->sendMessage(message,0);
		};

		StateSynced(StateMachine* p) : StateMachine("Synced", p, "idle") {
			((RfidStateMachine*)p)->applyNetworkUI(*this,{EVENT_WIFI_LOST, EVENT_SYNC_ERROR, EVENT_SYNC_NOSUPERSCALE});
			((RfidStateMachine*)p)->onEnter("Synced",[this]() {
				sendTemporaryMessage(String()+APP_NAME+" synced with superscale !");
			});
		};
	private:
		void applyCommonUI(State& s, const std::vector<String>& exclude = {}) {
			auto isExcluded = [&](String ev) {
				for (auto x : exclude) if (x == ev) return true;
				return false;};
			if (!isExcluded(EVENT_RFID_KNOWN)) s.on(EVENT_RFID_KNOWN, "setinprogress");
			if (!isExcluded(EVENT_RFID_UNKNOWN)) s.on(EVENT_RFID_UNKNOWN, "generateinprogress");
			if (!isExcluded(EVENT_RFID_COMMAND)) s.on(EVENT_RFID_COMMAND, "commandinprogress");
			if (!isExcluded(EVENT_RFID_ATTACHCOMMAND)) s.on(EVENT_RFID_ATTACHCOMMAND, "attachcommand");
			if (!isExcluded(EVENT_RFID_ERASE)) s.on(EVENT_RFID_ERASE, "erasecommand");
			if (!isExcluded(EVENT_RFID_UNDO)) s.on(EVENT_RFID_UNDO, "undocommand");
			if (!isExcluded(EVENT_RFID_RFIDUNDO)) s.on(EVENT_RFID_RFIDUNDO, "rfidundocommand").attachGuard([this](EventSource& e){
				if(!dataman->canUndo()) {
					briefErrorFB("no operation to undo");
					return false;
				}
				return true;
			});
			if (!isExcluded(EVENT_BUTTON_PRESS)) s.on(EVENT_BUTTON_PRESS, "saving");
			if (!isExcluded(EVENT_RFID_BUSY)) s.on(EVENT_RFID_BUSY,s.name).attachAction([this](EventSource& e){
				briefErrorFB();
			});


		}

		void briefErrorFB(String error=""){
			vbeeper->playSpecial("shortfail");
			rgbled->playColor(LED_ERROR_COLOR);
			String e;
			if(error.length()>0) e=String()+": "+error;
			sendTemporaryMessage(String()+APP_NAME+" encountered an error"+e);
		}
		void writeConfirmFB(){
			vbeeper->playSpecial("highbeep");
			rgbled->playColor(LED_CONFIRMSAVE_COLOR);
		}
		void writeConfirmRfidFB(){
			vbeeper->playSpecial("funkybeep");
			rgbled->playColor(LED_CONFIRMSYNC_COLOR);
			sendTemporaryMessage(String()+APP_NAME+" synced with superscale !");
		}

		// IDLE OPERATIONAL STATE : ready to scan
		State idle{"idle", this, [this](State& s) {
			s.onEnter([this]() {
				vbeeper->playSpecial("ready");
				rgbled->playColor(LED_SYNCED_COLOR);
				sendPermanentMessage(String()+APP_NAME+" ready: scan a new rfid tag !");
			});
			applyCommonUI(s);
		}};

		///////////////////////////////////////////////////
		// SAVING
		State saving{"saving", this, [this](State& s) {
			s.onEnter([this]() {
				vbeeper->playSpecial("shortup");
				rgbled->playColor(LED_SAVEINPROGRESS_COLOR);
				dataman->triggerSave();
			});
			s.on(EVENT_SAVE_SUCCESS, "idle").attachAction([this](EventSource& e) {
				writeConfirmFB();
				//				sendTemporaryMessage(String()+APP_NAME+" saved block "+dataman->getCurrentID()+", "+dataman->getCurrentCommand());
			});
			s.on(EVENT_SAVE_FAIL, "idle").attachAction([this](EventSource& e) {
				briefErrorFB();
			});
		}};

		///////////////////////////////////////////////////
		// SETTING IN PROGRESS : waiting for server reply
		State setinprogress{"setinprogress", this, [this](State& s) {
			s.onEnter([this]() {
				vbeeper->playSpecial("reconnu");
				rgbled->playColor(LED_SETINPROGRESS_COLOR);
				dataman->fetchSetId();
			});
			s.on(EVENT_SET_SUCCESS, "setready").attachAction([this](EventSource& e) {
				vbeeper->playSpecial("ok");
				rgbled->playColor(LED_SETREADY_COLOR);
			});
			s.on(EVENT_SET_FAIL, "idle").attachAction([this](EventSource& e) {
				briefErrorFB();
			});
		}};
		// SET READY: got ok from server
		State setready{"setready", this, [this](State& s) {
			s.onEnter([this]() {
				sendPermanentMessage(String()+APP_NAME+" set id to "+dataman->getCurrentID());
			});
			applyCommonUI(s, {EVENT_BUTTON_PRESS});
			s.on(EVENT_BUTTON_PRESS, "saving");
		}};

		///////////////////////////////////////////////////
		// GENERATE IN PROGRESS : waiting ok & id from server
		State generateinprogress{"generateinprogress", this, [this](State& s) {
			s.onEnter([this]() {
				vbeeper->playSpecial("reconnu2");
				rgbled->playColor(LED_GENERATEINPROGRESS_COLOR);
				dataman->fetchGenerate();
			});
			s.on(EVENT_GENERATE_SUCCESS, "generateready").attachAction([this](EventSource& e) {
				// Validate the generated ID before proceeding
				vbeeper->playSpecial("ok");
				rgbled->playColor(LED_GENERATEREADY_COLOR);
			}).attachGuard([this](EventSource& e) {
				if (!dataman->validateGeneratedId(dataman->getCurrentID())) {
					Serial.println("State machine: Generated ID already exists in database, rejecting.");
					dispatchEventSM(EVENT_GENERATEID_INVALID);// Force transition to idle on duplicate ID
					sendTemporaryMessage(String()+APP_NAME+": Generated ID already exists in database, rejecting!");
					return false;
				};
				return true;
			});
			s.on(EVENT_GENERATEID_INVALID, "idle").attachAction([this](EventSource& e) {
				briefErrorFB("generating ID failed: id invalid");
				briefErrorFB("generating ID failed: id invalid");
			});
			s.on(EVENT_GENERATE_FAIL, "idle").attachAction([this](EventSource& e) {
				briefErrorFB("generating ID failed");
			});

		}};
		// GENERATE IS READY
		State generateready{"generateready", this, [this](State& s) {
			s.onEnter([this]() {
				sendPermanentMessage(String()+APP_NAME+" generated id "+dataman->getCurrentID()+" for tag "+dataman->getCurrentTag());
			});
			applyCommonUI(s, {EVENT_BUTTON_PRESS});
			s.on(EVENT_BUTTON_PRESS, "savingGenerate");
		}};
		// SAVING GENERATE
		State savingGenerate{"savingGenerate", this, [this](State& s) {
			s.onEnter([this]() {
				vbeeper->playSpecial("shortup");
				rgbled->playColor(LED_SAVEINPROGRESS_COLOR);
				dataman->triggerSave();
			});
			s.on(EVENT_SAVE_SUCCESS, "rfiddbsyncing").attachAction([this](EventSource& e) {
					dataman->saveCurrentRfid();
				writeConfirmFB();
				sendTemporaryMessage(String()+APP_NAME+" associated tag "+dataman->getCurrentTag()+"with id "+dataman->getCurrentID());
			}).attachGuard([this](EventSource& e) {
				if (!dataman->validateGeneratedId(dataman->getCurrentID())) {
					Serial.println("State machine: Generated ID already exists in database, rejecting.");
					briefErrorFB();
					dispatchEventSM(EVENT_GENERATEID_INVALID);// Force transition to idle on duplicate ID
					sendTemporaryMessage(String()+APP_NAME+": Generated ID already exists in database, rejecting!");
					return false;
				};
				return true;
			});
			s.on(EVENT_GENERATEID_INVALID, "idle").attachAction([this](EventSource& e) {
				briefErrorFB("generating ID failed  !");
				briefErrorFB("generating ID failed  !");
			});
			s.on(EVENT_SAVE_FAIL, "idle").attachAction([this](EventSource& e) {
				briefErrorFB();
			});
		}};
		State rfiddbsyncing{"rfiddbsyncing", this, [this](State& s) {
			s.onEnter([this]() {
				vbeeper->playSpecial("shortup");
				rgbled->playColor(LED_SAVEINPROGRESS_COLOR);
			});
			s.on(EVENT_SYNC_SUCCESS, "idle").attachAction([this](EventSource& e) {
				writeConfirmRfidFB();
			});
			s.on(EVENT_PATCH_SUCCESS, "idle").attachAction([this](EventSource& e) {
				writeConfirmRfidFB();
			});
			s.on(EVENT_SYNC_ERROR, "idle").attachAction([this](EventSource& e) {
				briefErrorFB();
			});
			s.on(EVENT_PATCH_ERROR, "idle").attachAction([this](EventSource& e) {
				briefErrorFB();
			});
		}};

		///////////////////////////////////////////////////
		// COMMAND SET IN PROGRESS: set command on superscale
		State commandinprogress{"commandinprogress", this, [this](State& s) {
			s.onEnter([this]() {
				vbeeper->playSpecial("reconnu3");
				rgbled->playColor(LED_COMMANDINPROGRESS_COLOR);
				dataman->fetchSetCommand(dataman->getCurrentCommand());
			});
			s.on(EVENT_SET_COMMAND_SUCCESS, "idle").attachAction([this](EventSource& e) {
				vbeeper->playSpecial("ok");
				rgbled->playColor(LED_COMMAND_COLOR);
			});
			s.on(EVENT_SET_COMMAND_FAIL, "idle").attachAction([this](EventSource& e) {
				briefErrorFB();
			});
		}};

		///////////////////////////////////////////////////
		// ATTACH COMMAND
		State attachcommand{"attachcommand", this, [this](State& s) {
			s.onEnter([this]() {
				vbeeper->playSpecial("reconnu4");
				rgbled->playColor(LED_ATTACHCOMMAND_BLINKCOLOR);
				dataman->fetchCommand();
				sendPermanentMessage(String()+APP_NAME+" attaching current command ");
			});
			applyCommonUI(s,{EVENT_BUTTON_PRESS,EVENT_RFID_UNKNOWN, EVENT_RFID_KNOWN});
			s.on(EVENT_RFID_UNKNOWN, "attachcommandnewrfid").attachAction([this](EventSource& e) {
				vbeeper->playSpecial("ready");
				rgbled->playColor(LED_ATTACHCOMMAND_COLOR);
			});
			s.on(EVENT_RFID_KNOWN, "idle").attachAction([this](EventSource& e) {
				briefErrorFB();
			});
			s.on(EVENT_BUTTON_PRESS, "idle").attachAction([this](EventSource& e) {
				briefErrorFB();
			});
		}};
		// ATTACH COMMAND and NEW RFID
		State attachcommandnewrfid{"attachcommandnewrfid", this, [this](State& s) {
			applyCommonUI(s, {EVENT_BUTTON_PRESS});
			s.on(EVENT_BUTTON_PRESS, "rfiddbsyncing").attachAction([this](EventSource& e) {
				dataman->saveCurrentRfid(String()+"command:"+dataman->getCurrentCommand());
				writeConfirmFB();
				sendPermanentMessage(String()+APP_NAME+" attached command "+dataman->getCurrentCommand()+" to tag "+dataman->getCurrentTag());

			});
		}};

		///////////////////////////////////////////////////
		// ERASE
		State erasecommand{"erasecommand", this, [this](State& s) {
			s.onEnter([this]() {
				vbeeper->playSpecial("siren");
				rgbled->playColor(LED_ERASE_BLINKCOLOR);
				sendPermanentMessage(String()+APP_NAME+" erasing tag: scan to erase ");
			});
			s.on(EVENT_BUTTON_PRESS, "idle").attachAction([this](EventSource& e) {
				briefErrorFB();
			});
			s.on(EVENT_RFID_UNKNOWN, "idle").attachAction([this](EventSource& e) {
				briefErrorFB();
			});
			s.on(EVENT_RFID_KNOWN, "erasecommandconfirm").attachAction([this](EventSource& e) {
				vbeeper->playSpecial("shortup");
				rgbled->playColor(LED_ERASE_BLINKFASTCOLOR);
			});
			s.on(EVENT_RFID_COMMAND, "erasecommandconfirm").attachAction([this](EventSource& e) {
				vbeeper->playSpecial("shortup");
				rgbled->playColor(LED_ERASE_BLINKFASTCOLOR);
			});
		}};
		State erasecommandconfirm{"erasecommandconfirm", this, [this](State& s) {
			s.onEnter([this]() {
				sendPermanentMessage(String()+APP_NAME+" erasing tag: confirm to erase tag "+dataman->getCurrentTag());
			});
			applyCommonUI(s, {EVENT_BUTTON_PRESS});
			s.on(EVENT_BUTTON_PRESS, "rfiddbsyncing").attachAction([this](EventSource& e) {
				dataman->eraseCurrentRfid();
				vbeeper->playSpecial("superhighbeep");
				rgbled->playColor(LED_ERASE_COLOR);
			});
		}};

		///////////////////////////////////////////////////
		// UNDO
		State undocommand{"undocommand", this, [this](State& s) {
			applyCommonUI(s, {EVENT_BUTTON_PRESS});
			s.onEnter([this]() {
				vbeeper->playSpecial("bungee");
				rgbled->playColor(LED_UNDO_BLINKCOLOR);
				sendPermanentMessage(String()+APP_NAME+" undo: press button to undo last action ");
			});
			s.on(EVENT_BUTTON_PRESS, "undoexecute").attachAction([this](EventSource& e) {
				vbeeper->playSpecial("shortup");
				rgbled->playColor(LED_UNDO_BLINKFASTCOLOR);
			});
		}};
		State undoexecute{"undoexecute", this, [this](State& s) {
			s.onEnter([this]() {
				dataman->fetchUndo();
			});
			s.on(EVENT_UNDO_SUCCESS, "idle").attachAction([this](EventSource& e) {
				vbeeper->playSpecial("superhighbeep");
				rgbled->playColor(LED_UNDO_COLOR);
				sendTemporaryMessage(String()+APP_NAME+" undo successful");
			});
			s.on(EVENT_UNDO_FAIL, "idle").attachAction([this](EventSource& e) {
				briefErrorFB();
			});
		}};

		///////////////////////////////////////////////////
		// RFID UNDO
		State rfidundocommand{"rfidundocommand", this, [this](State& s) {
			applyCommonUI(s, {EVENT_BUTTON_PRESS});
			s.onEnter([this]() {
				vbeeper->playSpecial("exclamation");
				rgbled->playColor(LED_RFIDUNDO_BLINKCOLOR);
				String undoDesc = dataman->getLastUndoDescription();
				sendPermanentMessage(String()+APP_NAME+" rfid-undo: press to undo "+undoDesc);
			});
			s.on(EVENT_BUTTON_PRESS, "rfidundoexecute").attachAction([this](EventSource& e) {
				vbeeper->playSpecial("shortup");
				rgbled->playColor(LED_RFIDUNDO_BLINKFASTCOLOR);
			});
		}};
		State rfidundoexecute{"rfidundoexecute", this, [this](State& s) {
			s.onEnter([this]() {
				dataman->fetchRfidUndo();
			});
			s.on(EVENT_RFIDUNDO_SUCCESS, "rfiddbsyncing").attachAction([this](EventSource& e) {
				vbeeper->playSpecial("superhighbeep");
				rgbled->playColor(LED_RFIDUNDO_COLOR);
				sendTemporaryMessage(String()+APP_NAME+" rfid-undo successful");
			});
			s.on(EVENT_RFIDUNDO_FAIL, "idle").attachAction([this](EventSource& e) {
				briefErrorFB();
			});
		}};
	} stateSynced={this};

	void setDataManager(DataManager *dataman0){dataman=dataman0;stateSynced.dataman=dataman0;};
	void setRGBLed(RGBLed *rgbled0){rgbled=rgbled0;stateSynced.rgbled = rgbled0;};
	void setVBeeper(VBeeper *vbeeper0){vbeeper=vbeeper0;stateSynced.vbeeper = vbeeper0;};
} rsm;


void initSM(RGBLed *rgbled0, VBeeper *vbeeper0, DataManager *dataman0) {
	Serial.println("Initializing FSM...");
	vbeeper0->playSpecial(BUZZER_BOOT);
	rgbled0->playColor(LED_INIT_COLOR);



	//	rgbled0->playColor("rainbow");
	//	for(int i=1500;i>0;i--) {rgbled0->update();delay(10);}
	//	rgbled0->playColor("rainbow");
	rsm.setRGBLed(rgbled0);
	rsm.setVBeeper(vbeeper0);
	rsm.setDataManager(dataman0);
	rsm.start();
}
StateMachine &getSM(){return rsm;}




void processQueue() { // The callback that pulls from the queue
    rsm.dispatch(evtQueue.front()); // 1. Execute the oldest event
    evtQueue.pop();                 // 2. Remove it from the queue
    if (!evtQueue.empty()) evtTicker.once_ms(3, processQueue); // 3. If more remain, trigger again in 3ms
}

void dispatchEventSM(String eventname) {
    evtQueue.push(eventname);
    if (evtQueue.size() == 1) evtTicker.once_ms(3, processQueue); // If size is 1, the ticker was idle. Kickstart it.
}



#endif
