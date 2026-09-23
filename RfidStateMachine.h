#ifndef RFIDSTATEMACHINE_H
#define RFIDSTATEMACHINE_H

#include "RGBLed.h"
#include <device/Beeper.H>
#include "DataManager.h"

#include "Definitions.h"


#pragma once
#include "StateMachine.h"

#define SYNC_WAIT_DURATION 5000


class RfidStateMachine : public StateMachine {
protected:
	RGBLed *rgbled = nullptr;
	VBeeper *vbeeper = nullptr;
	DataManager *dataman = nullptr;

	Ticker smticker;

public:
	RfidStateMachine() : StateMachine("RfidStateMachine", nullptr, "WifiDisconnected") {}

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

		StateSynced(StateMachine* p) : StateMachine("Synced", p, "idle") {
			((RfidStateMachine*)p)->applyNetworkUI(*this,{EVENT_WIFI_LOST, EVENT_SYNC_ERROR, EVENT_SYNC_NOSUPERSCALE});
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
			if (!isExcluded(EVENT_BUTTON_PRESS)) s.on(EVENT_BUTTON_PRESS, "saving");
			if (!isExcluded(EVENT_RFID_BUSY)) s.on(EVENT_RFID_BUSY,s.name).attachAction([this](EventSource& e){
					briefErrorFB();
			});


		}

		void briefErrorFB(){
			vbeeper->playSpecial("shortfail");
			rgbled->playColor(LED_ERROR_COLOR);
		}
		void writeConfirmFB(){
			vbeeper->playSpecial("highbeep");
			rgbled->playColor(LED_CONFIRMSAVE_COLOR);
		}
		void writeConfirmRfidFB(){
			vbeeper->playSpecial("funkybeep");
			rgbled->playColor(LED_CONFIRMSYNC_COLOR);
		}

		// IDLE OPERATIONAL STATE : ready to scan
		State idle{"idle", this, [this](State& s) {
			s.onEnter([this]() {
				vbeeper->playSpecial("ready");
				rgbled->playColor(LED_SYNCED_COLOR);
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
					return false;
				};
				return true;
			});
			s.on(EVENT_GENERATEID_INVALID, "idle").attachAction([this](EventSource& e) {
				briefErrorFB();
				briefErrorFB();
			});
			s.on(EVENT_GENERATE_FAIL, "idle").attachAction([this](EventSource& e) {
				briefErrorFB();
			});
			s.on("ID_INVALID", "idle").attachAction([this](EventSource& e) {
				briefErrorFB();
			});
		}};
		// GENERATE IS READY
		State generateready{"generateready", this, [this](State& s) {
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
			}).attachGuard([this](EventSource& e) {
				if (!dataman->validateGeneratedId(dataman->getCurrentID())) {
					Serial.println("State machine: Generated ID already exists in database, rejecting.");
					briefErrorFB();
					dispatchEventSM("EVENT_GENERATEID_INVALID");// Force transition to idle on duplicate ID
					return false;
				};
				return true;
			});
			s.on(EVENT_GENERATEID_INVALID, "idle").attachAction([this](EventSource& e) {
				briefErrorFB();
				briefErrorFB();
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
			});
		}};

		///////////////////////////////////////////////////
		// ERASE
		State erasecommand{"erasecommand", this, [this](State& s) {
			s.onEnter([this]() {
				vbeeper->playSpecial("siren");
				rgbled->playColor(LED_ERASE_BLINKCOLOR);
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
			applyCommonUI(s, {EVENT_BUTTON_PRESS});
			s.on(EVENT_BUTTON_PRESS, "rfiddbsyncing").attachAction([this](EventSource& e) {
				dataman->eraseCurrentRfid();
				vbeeper->playSpecial("superhighbeep");
				rgbled->playColor(LED_ERASE_COLOR);
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
	rsm.setRGBLed(rgbled0);
	rsm.setVBeeper(vbeeper0);
	rsm.setDataManager(dataman0);
	rsm.start();
}
StateMachine &getSM(){return rsm;}

void dispatchEventSM(String eventname) {
	rsm.dispatch(eventname);
}



#endif
