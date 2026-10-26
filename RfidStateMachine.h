#ifndef RFIDSTATEMACHINE_H
#define RFIDSTATEMACHINE_H

#include <queue>
#include <Ticker.h>
#include <device/Beeper.h>
#include "RGBLed.h"
#include "DataManager.h"
#include "Definitions.h"
#include "StateMachine.h"


#define SYNC_WAIT_DURATION 5000

std::queue<String> evtQueue;




class RfidStateMachine : public StateMachine {
protected:
	RGBLed *rgbled = nullptr;
	VBeeper *vbeeper = nullptr;
	DataManager *dataman = nullptr;

	Ticker smticker;
	bool commavailable=false;

public:

	RfidStateMachine() : StateMachine("RfidStateMachine", nullptr, "WifiDisconnected") {}

	bool isCommAvailable(){return commavailable;}
	void setCommAvailable(bool newvalue){commavailable=newvalue;}

	void sendTemporaryMessage(String message){
		if(!dataman) return;
		if(isCommAvailable()) dataman->sendMessage(message,6);
	}
	void sendPermanentMessage(String message){
		if(!dataman) return;
		if(isCommAvailable()) dataman->sendMessage(message,0);
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
			dataman->getRfidDB()->syncRemoteFile();
		});
		applyNetworkUI(s,{EVENT_WIFI_LOST, EVENT_SYNC_ERROR, EVENT_SYNC_NOSUPERSCALE});
		on("WifiConnected", EVENT_SYNC_SUCCESS, "Synced").attachAction([this](EventSource& e) {
						Serial.println("FSM: WifiConnected: EVENT_SYNC_SUCCESS! Moving to Synced.");
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
		bool firsttime=true;
		String printmessage;
		Ticker recoveryTicker;

		void sendTemporaryMessage(String message){
			if(!dataman) return;
			if (((RfidStateMachine*)parent)->isCommAvailable()) dataman->sendMessage(message,6);
		}
		void sendPermanentMessage(String message){
			if(!dataman) return;
			if (((RfidStateMachine*)parent)->isCommAvailable()) dataman->sendMessage(message,0);
		};

		StateSynced(StateMachine* p) : StateMachine("Synced", p, "idle") {
			RfidStateMachine* myrsm = static_cast<RfidStateMachine*>(p);
			myrsm->applyNetworkUI(*this,{EVENT_WIFI_LOST, EVENT_SYNC_ERROR, EVENT_SYNC_NOSUPERSCALE});
			myrsm->onEnter("Synced",[this, myrsm]() {
				firsttime=true;
				myrsm->setCommAvailable(true);
			});
			p->onLeave("Synced",[this, myrsm]() {
				myrsm->setCommAvailable(false);
			});
		};

		int syncAttempts = 0, patchAttempts = 0;
		static const int MAX_SYNC_ATTEMPTS = 3;
		static const int MAX_PATCH_ATTEMPTS = 3;

	private:
		void applyCommonUI(State& s, const std::vector<String>& exclude = {}) {
			auto isExcluded = [&](String ev) {
				for (auto x : exclude) if (x == ev) return true;
				return false;};
			if (!isExcluded(EVENT_RFID_KNOWN)) s.on(EVENT_RFID_KNOWN, "setinprogress");
			if (!isExcluded(EVENT_RFID_UNKNOWN)) s.on(EVENT_RFID_UNKNOWN, "generateinprogress");
			if (!isExcluded(EVENT_RFID_COMMAND)) {
				s.on(EVENT_RFID_COMMAND, "commandsaveinprogress").attachGuard([this](EventSource& e) {
					return dataman->getCurrentCommand().startsWith("@");
				});
				s.on(EVENT_RFID_COMMAND, "commandinprogress").attachGuard([this](EventSource& e) {
					String command=dataman->getCurrentCommand();
					return !command.startsWith("@") && !command.startsWith("#");
				});
				s.on(EVENT_RFID_COMMAND, "commandtaggetinprogress").attachGuard([this](EventSource& e) {
					return dataman->getCurrentCommand().startsWith("#");
				});
			}
			if (!isExcluded(EVENT_RFID_ATTACHCOMMAND)) s.on(EVENT_RFID_ATTACHCOMMAND, "attachcommand");
			if (!isExcluded(EVENT_RFID_ERASE)) s.on(EVENT_RFID_ERASE, "erasecommand");
			if (!isExcluded(EVENT_RFID_UNDO)) s.on(EVENT_RFID_UNDO, "undocommand");
			if (!isExcluded(EVENT_RFID_RFIDUNDO)) s.on(EVENT_RFID_RFIDUNDO, "rfidundocommand").attachGuard([this](EventSource& e){
				if(!dataman->getRfidDB()->canUndo()) {
					briefErrorFB("no operation to undo",1);
					return false;
				}
				return true;
			});
			if (!isExcluded(EVENT_BUTTON_PRESS)) s.on(EVENT_BUTTON_PRESS, "saving");
			if (!isExcluded(EVENT_RFID_BUSY)) s.on(EVENT_RFID_BUSY,s.name).attachAction([this](EventSource& e){
				briefErrorFB();
			});
		}

		void briefErrorFB(String error="", bool makeitpermanent=false){
			vbeeper->playSpecial("shortfail");
			rgbled->playColor(LED_ERROR_COLOR);
			String e;
			if(error.length()>0) e=String()+": "+error;
			if(makeitpermanent) sendPermanentMessage(String()+APP_NAME+" encountered an error"+e);
			else sendTemporaryMessage(String()+APP_NAME+" encountered an error"+e);
		}
		void briefErrorFBNoMessage(String error="", bool makeitpermanent=false){
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
			sendTemporaryMessage(String()+APP_NAME+" synced with superscale !");
		}

		/////////////////////////////////////////////////////////
		// IDLE OPERATIONAL STATE : ready to scan
		State idle{"idle", this, [this](State& s) {
			s.onEnter([this]() {
				vbeeper->playSpecial("ready");
				rgbled->playColor(LED_SYNCED_COLOR);
				Serial.println(String()+"First:"+firsttime);
				if(firsttime) sendPermanentMessage(String()+"Ready to scan an RFID tag!");
				else sendTemporaryMessage(String()+"Ready to scan an RFID tag!");
				firsttime=false;
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
				sendPermanentMessage(String()+"Saved on superscale block "+dataman->getCurrentID()+", "+dataman->getCurrentCommand());
			});
			s.on(EVENT_SAVE_FAIL, "idle").attachAction([this](EventSource& e) {
				briefErrorFB(String()+"Failed to save on superscale block with id '"+dataman->getCurrentID()+"', command '"+dataman->getCurrentCommand()+"'. "+dataman->getStatusText(), true);
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
				sendPermanentMessage(String()+"ID tag scanned "+dataman->getCurrentTag()+": ID set to "+dataman->getCurrentID()+", press button to save");
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
				dataman->getRfidDB()->syncRemoteFile();		
			});
			s.on(EVENT_SYNC_SUCCESS, nullptr).attachAction([this](EventSource& e) {
				vbeeper->playSpecial("ok");
				dataman->fetchGenerate();
			});
			s.on(EVENT_SYNC_ERROR, "idle").attachAction([this](EventSource& e) {
				briefErrorFB("Sync error during generate operation");
			});

			s.on(EVENT_GENERATE_SUCCESS, "generateready").attachAction([this](EventSource& e) {
				vbeeper->playSpecial("ok");
				rgbled->playColor(LED_GENERATEREADY_COLOR);
			}).attachGuard([this](EventSource& e) {
				return !dataman->getRfidDB()->idExists(dataman->getCurrentID());// Validate the generated ID before proceeding
			});
			s.on(EVENT_GENERATE_SUCCESS, "idle").attachAction([this](EventSource& e) {
				Serial.println("State machine: Generated ID already exists in database, rejecting.");
				briefErrorFB(String()+"Generated ID already exists in database, rejecting!",true);
			}).attachGuard([this](EventSource& e) {
				return dataman->getRfidDB()->idExists(dataman->getCurrentID());// Validate the generated ID before proceeding
			});
			s.on(EVENT_GENERATE_FAIL, "idle").attachAction([this](EventSource& e) {
				briefErrorFB("generating ID failed",true);
			});

		}};
		// GENERATE IS READY
		State generateready{"generateready", this, [this](State& s) {
			s.onEnter([this]() {
				sendPermanentMessage(String()+"Ready to associate empty tag "+dataman->getCurrentTag()+" with ID "+dataman->getCurrentID()+", press button to save");
			});
			applyCommonUI(s, {EVENT_BUTTON_PRESS});
			// skip saving transition
			s.on(EVENT_BUTTON_PRESS, "rfiddbsyncing").attachGuard([this](EventSource& e) {
				String disableRfidIdSave = dataman->getDisableRfidIdSave();
				disableRfidIdSave.toLowerCase();
				//				Serial.println(String()+"State machine: "+DISABLESAVE_KEYWORD+" is '"+disableRfidIdSave+"'");
				return (disableRfidIdSave != "0" && disableRfidIdSave != "false");
			}).attachAction([this](EventSource& e) {
				Serial.println(String()+"State machine: Saving current id:"+dataman->getCurrentID()+" with tag:"+dataman->getCurrentTag());
				dataman->getRfidDB()->set(dataman->getCurrentTag(), dataman->getCurrentID());
			});

			s.on(EVENT_BUTTON_PRESS, "savingGenerate").attachGuard([this](EventSource& e) {
				String disableRfidIdSave = dataman->getDisableRfidIdSave();
				disableRfidIdSave.toLowerCase();
				return !(disableRfidIdSave != "0" && disableRfidIdSave != "false");
			});
		}};
		// SAVING GENERATE
		State savingGenerate{"savingGenerate", this, [this](State& s) {
			s.onEnter([this]() {
				vbeeper->playSpecial("shortup");
				rgbled->playColor(LED_SAVEINPROGRESS_COLOR);
				dataman->triggerSave();
			});
			s.on(EVENT_SAVE_SUCCESS, "rfiddbsyncing").attachAction([this](EventSource& e) {
				dataman->getRfidDB()->set(dataman->getCurrentTag(), dataman->getCurrentID());
				writeConfirmFB();
				printmessage=String()+"Successfully associated ID "+dataman->getCurrentID()+" with tag "+dataman->getCurrentTag()+" !";
				//				sendPermanentMessage(String()+APP_NAME+" associating tag "+dataman->getCurrentTag()+" with ID "+dataman->getCurrentID());
			}).attachGuard([this](EventSource& e) {
				return !dataman->getRfidDB()->idExists(dataman->getCurrentID());
			});
			s.on(EVENT_SAVE_SUCCESS, "idle").attachGuard([this](EventSource& e) {
				return dataman->getRfidDB()->idExists(dataman->getCurrentID());
			}).attachAction([this](EventSource& e) {
				Serial.println("State machine: Generated ID already exists in database, rejecting.");
				briefErrorFB("Generated ID already exists in database, rejecting!");
				 // Force transition to idle on duplicate ID
			//	sendTemporaryMessage(String()+"Generated ID already exists in database, rejecting!");
			});
			s.on(EVENT_SAVE_FAIL, "idle").attachAction([this](EventSource& e) {
				briefErrorFB(dataman->getStatusText());
			});
		}};

		////////////////////////
		// RFID SYNC
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
				if(printmessage.length()>0) sendPermanentMessage(printmessage);
				printmessage="";
			});
			s.on(EVENT_SYNC_ERROR, "idle").attachAction([this](EventSource& e) {
				briefErrorFB("Sync error");
			});
			s.on(EVENT_PATCH_ERROR, "idle").attachAction([this](EventSource& e) {
				briefErrorFB("Patch error");
			});
#ifdef RFID_RECOVERY_ENABLED
			s.on(EVENT_PATCH_ERROR, "rfiddbrecovery");
#endif
		}};
#ifdef RFID_RECOVERY_ENABLED
		////////////////////////
		// RFID RECOVERY
		State rfiddbrecovery{"rfiddbrecovery", this, [this](State& s) {
			s.onEnter([this]() {
				syncAttempts = 0; patchAttempts = 0;
				Serial.println("Patch conflict, entering recovery state");
				rgbled->playColor(LED_RECOVERY_COLOR);
				sendTemporaryMessage(String()+"File conflict detected, syncing...");
				dataman->getRfidDB()->startRecovery();
			});
			s.onLeave([this]() { recoveryTicker.detach(); });
			// Sync success: try patch
			s.on(EVENT_RECOVERY_SYNC_SUCCESS, "rfiddbrecovery").attachAction([this](EventSource& e) {
				syncAttempts++;
				Serial.printf("Sync success (%d/%d), retrying patch\n", syncAttempts, MAX_SYNC_ATTEMPTS);
				recoveryTicker.once_ms(syncAttempts * 1000, [this]() { dataman->getRfidDB()->retryPendingPatch(); });
			});
			// Sync error: retry sync or give up
			s.on(EVENT_RECOVERY_SYNC_ERROR, "rfiddbrecovery").attachGuard([this](EventSource& e) {
				return syncAttempts < MAX_SYNC_ATTEMPTS;;
			}).attachAction([this](EventSource& e) {
				syncAttempts++;
				Serial.printf("During recovery sync error (%d/%d), retrying\n", syncAttempts, MAX_SYNC_ATTEMPTS);
				recoveryTicker.once_ms(syncAttempts * 1000, [this]() { syncAttempts++;dataman->getRfidDB()->startRecovery(); });
			});
			s.on(EVENT_RECOVERY_SYNC_ERROR, "idle").attachGuard([this](EventSource& e) {
				return !(syncAttempts < MAX_SYNC_ATTEMPTS);
			}).attachAction([this](EventSource& e) {
				syncAttempts++;
				Serial.println("Max sync attempts reached, giving up");
				briefErrorFB("Max sync attempts reached, giving up", true);
			});
			// Patch success: done
			s.on(EVENT_PATCH_SUCCESS, "idle").attachAction([this](EventSource& e) {
				patchAttempts++;
				Serial.println("Recovery patch success");
				writeConfirmRfidFB();
				if(printmessage.length()>0) sendPermanentMessage(String()+"Recovery successful:"+printmessage);
				else sendPermanentMessage(String()+"Recovery successful!");
				printmessage="";
			});
			// Patch error: retry sync & patch again or give up
			s.on(EVENT_PATCH_ERROR, "rfiddbrecovery").attachGuard([this](EventSource& e) {	// PATCH error don't patch again, it sync again first
				return patchAttempts < MAX_PATCH_ATTEMPTS;;
			}).attachAction([this](EventSource& e) {		
				Serial.printf("Patch error (%d/%d), retrying\n", patchAttempts, MAX_PATCH_ATTEMPTS);
				recoveryTicker.once_ms(patchAttempts * 1000, [this]() {dataman->getRfidDB()->retryPendingPatch(); });
			});
			s.on(EVENT_PATCH_ERROR, "idle").attachGuard([this](EventSource& e) {
				return !(patchAttempts < MAX_PATCH_ATTEMPTS);
			}).attachAction([this](EventSource& e) {
				Serial.println("Max patch attempts reached, giving up");
				briefErrorFB("Max patch attempts reached, giving up", true);
			});
			// Cannot find tag in new file, give up
			s.on(EVENT_RECOVERY_GONE, "idle").attachAction([this](EventSource& e) {
				Serial.println("Rfid tag removed by another client");
				briefErrorFB("Rfid tag was removed by another client", true);
			});
		}};
#endif
		///////////////////////////////////////////////////
		// COMMAND SET IN PROGRESS: set command on superscale : Classic tag
		State commandinprogress{"commandinprogress", this, [this](State& s) {
			s.onEnter([this]() {
				vbeeper->playSpecial("reconnu3");
				rgbled->playColor(LED_COMMANDINPROGRESS_COLOR);
				dataman->fetchSetCommand(dataman->getCurrentCommand());
			});
			s.on(EVENT_SET_COMMAND_SUCCESS, "idle").attachAction([this](EventSource& e) {
				vbeeper->playSpecial("ok");
				rgbled->playColor(LED_COMMAND_COLOR);
				sendPermanentMessage(String()+"Command classic tag scanned "+dataman->getCurrentTag()+ ": Command set to "+dataman->getCurrentCommand()+" !");
			});
			s.on(EVENT_SET_COMMAND_FAIL, "idle").attachAction([this](EventSource& e) {
				String message=String()+ "Command classic tag scanned "+dataman->getCurrentTag()+ ": failed to set Command to "+dataman->getCurrentCommand()+"";
				briefErrorFB(message,1);
			});
		}};
		// COMMAND SET IN PROGRESS: set command on superscale : set&save @ tag
		State commandsaveinprogress{"commandsaveinprogress", this, [this](State& s) {
			s.onEnter([this]() {
				vbeeper->playSpecial("reconnu3");
				rgbled->playColor(LED_COMMANDINPROGRESS_COLOR);
				dataman->fetchSaveCommand(dataman->getCurrentCommand());
			});
			s.on(EVENT_SAVE_COMMAND_SUCCESS, "idle").attachAction([this](EventSource& e) {
				writeConfirmFB();
				sendPermanentMessage(String()+"Command set&save @ tag scanned "+dataman->getCurrentTag()+ ": Command set to "+dataman->getCurrentCommand()+" and saved with ID "+dataman->getCurrentID()+" !");
			});
			s.on(EVENT_SAVE_COMMAND_FAIL, "idle").attachAction([this](EventSource& e) {
				String message=String()+ "Command set&save @ tag scanned but failed to save";
				if(dataman->getStatusText()!="NOTOK") message+=String()+ ": "+dataman->getStatusText();
				briefErrorFB(message,1);
			});
		}};

		// COMMAND SET IN PROGRESS: set command on superscale : transient # tag
		State commandtaggetinprogress{"commandtaggetinprogress", this, [this](State& s) {
			s.onEnter([this]() {
				vbeeper->playSpecial("reconnu3");
				rgbled->playColor(LED_COMMANDINPROGRESS_COLOR);
				dataman->fetchCurrentLine();
			});
			s.on(EVENT_GET_LINE_SUCCESS, "commandtagsaveinprogress");
			s.on(EVENT_GET_LINE_FAIL, "idle").attachAction([this](EventSource& e) {
				briefErrorFB(dataman->getStatusText());
			});
		}};
		bool savesuccess=false;
		State commandtagsaveinprogress{"commandtagsaveinprogress", this, [this](State& s) {
			s.onEnter([this]() {
				rgbled->playColor(LED_SAVEINPROGRESS_COLOR);
				savesuccess=false;
				dataman->fetchSaveCommand(dataman->getCurrentCommand());
			});
			s.on(EVENT_SAVE_COMMAND_SUCCESS, "commandtagrestoreinprogress").attachAction([this](EventSource& e) {
				writeConfirmFB();
				savesuccess=true;
			});
			s.on(EVENT_SAVE_COMMAND_FAIL, "commandtagrestoreinprogress").attachAction([this](EventSource& e) {
				briefErrorFBNoMessage();
				savesuccess=false;
			});
		}};
		State commandtagrestoreinprogress{"commandtagrestoreinprogress", this, [this](State& s) {
			s.onEnter([this]() {
				rgbled->playColor(LED_COMMANDINPROGRESS_COLOR);
				dataman->fetchRestoreLine();
			});
			s.on(EVENT_RESTORE_LINE_SUCCESS, "idle").attachAction([this](EventSource& e) {
				vbeeper->playSpecial("ok");
				rgbled->playColor(LED_COMMAND_COLOR);
				if (savesuccess) sendPermanentMessage(String()+"Command transient # tag scanned "+dataman->getCurrentTag()+ ": Command set to "+dataman->getCurrentCommand()+", saved with ID "+dataman->getCurrentID()+", and restored back to "+dataman->getSavedLineCommand()+"!");
				else sendPermanentMessage(String()+"Command transient # tag scanned "+dataman->getCurrentTag()+ ": Command set to "+dataman->getCurrentCommand()+", but failed to save with ID "+dataman->getCurrentID()+", restored back to "+dataman->getSavedLineCommand()+"!");
				//			sendPermanentMessage(String()+" temporarily ran command from tag and restored previous Superscale line");
			});
			s.on(EVENT_RESTORE_LINE_FAIL, "idle").attachAction([this](EventSource& e) {
				briefErrorFB(dataman->getStatusText(), true);
			});
		}};

		bool requestactive=true;
		///////////////////////////////////////////////////
		// ATTACH COMMAND
		State attachcommand{"attachcommand", this, [this](State& s) {
			s.onEnter([this]() {
				vbeeper->playSpecial("reconnu4");
				rgbled->playColor(LED_ATTACHCOMMAND_BLINKCOLOR);
				requestactive=true;
				dataman->getRfidDB()->syncRemoteFile();		
			});
			s.onSelfInternal(EVENT_SYNC_SUCCESS).attachAction([this](EventSource& e) {
				requestactive=false;
				vbeeper->playSpecial("ok");
				rgbled->playColor(LED_ATTACHCOMMAND_WAITTAG_COLOR);
				sendPermanentMessage(String()+"Attach command super-tag scanned "+dataman->getCurrentTag()+": scan an empty tag to attach the current command");
			});
			s.on(EVENT_SYNC_ERROR, "idle").attachAction([this](EventSource& e) {
				briefErrorFB("Sync error during attach command operation");
			});

			applyCommonUI(s,{EVENT_BUTTON_PRESS,EVENT_RFID_UNKNOWN, EVENT_RFID_KNOWN});
			s.on(EVENT_GET_COMMAND_FAIL, "idle").attachAction([this](EventSource& e) {
				briefErrorFB();
				sendTemporaryMessage(String()+"Could not get command from superscale");
			});
			s.on(EVENT_RFID_UNKNOWN, "attachcommandnewrfid").attachAction([this](EventSource& e) {
				vbeeper->playSpecial("shortup");
				rgbled->playColor(LED_ATTACHCOMMAND_CONFIRM_COLOR);
				sendPermanentMessage(String()+"Empty tag scanned "+dataman->getCurrentTag()+", press button to fetch the current command settings and attach it");
			}).attachGuard([this](EventSource& e) {
				return requestactive==false;
			});
			s.on(EVENT_RFID_ATTACHCOMMAND, "idle").attachAction([this](EventSource& e) {
				briefErrorFB();
				sendTemporaryMessage(String()+"Cancelled attach command");
			}).attachGuard([this](EventSource& e) {
				return requestactive==false;
			});
			s.on(EVENT_RFID_KNOWN, "idle").attachAction([this](EventSource& e) {
				briefErrorFB();
				sendTemporaryMessage(String()+"Cancelled attach command");
			}).attachGuard([this](EventSource& e) {
				return requestactive==false;
			});
			s.on(EVENT_BUTTON_PRESS, nullptr).attachAction([this](EventSource& e) {
				briefErrorFB();	// maybe this is too much of a feedback and we need something smaller
				//				sendTemporaryMessage(String()+"Cancelled attach command");
			});
		}};
		// ATTACH COMMAND and NEW RFID
		State attachcommandnewrfid{"attachcommandnewrfid", this, [this](State& s) {
			applyCommonUI(s, {EVENT_BUTTON_PRESS});
			s.on(EVENT_BUTTON_PRESS, "attachcommandsettingsinprogress");
		}};
		State attachcommandsettingsinprogress{"attachcommandsettingsinprogress", this, [this](State& s) {
			s.onEnter([this]() {
				rgbled->playColor(LED_COMMANDINPROGRESS_COLOR);
				dataman->fetchCommand();
			});
			s.on(EVENT_GET_COMMAND_SUCCESS, "rfiddbsyncing").attachAction([this](EventSource& e) {
				String command=dataman->getCurrentCommand();
				String tagCommand=String()+COMMANDDOT_KEYWORD+command;
				dataman->getRfidDB()->set(dataman->getCurrentTag(), tagCommand);
				writeConfirmFB();
				sendPermanentMessage(String()+"Successfully attached Command "+command+" to tag "+dataman->getCurrentTag()+" !");
			});
		}};

		///////////////////////////////////////////////////
		// ERASE
		State erasecommand{"erasecommand", this, [this](State& s) {
			s.onEnter([this]() {
				vbeeper->playSpecial("siren");
				rgbled->playColor(LED_ERASE_BLINKCOLOR);
				dataman->getRfidDB()->syncRemoteFile();		
			});
			s.onSelfInternal(EVENT_SYNC_SUCCESS).attachAction([this](EventSource& e) {
				sendPermanentMessage(String()+"ERASE super-tag scanned "+dataman->getCurrentTag()+ ": scan non empty tag to erase it");	
			});
			s.on(EVENT_SYNC_ERROR, "idle").attachAction([this](EventSource& e) {
				vbeeper->playSpecial("ok");
				briefErrorFB("Sync error during erase operation");
			});
			
			s.onSelfInternal(EVENT_BUTTON_PRESS).attachAction([this](EventSource& e) {
				briefErrorFB();
				//sendPermanentMessage(String()+"Out of erase mode");
			});
			s.on(EVENT_RFID_UNKNOWN, "idle").attachAction([this](EventSource& e) {
				briefErrorFB();
				sendPermanentMessage(String()+"Out of erase mode");
			});
			s.on(EVENT_RFID_ERASE, "idle").attachAction([this](EventSource& e) {
				briefErrorFB();
				sendPermanentMessage(String()+"Out of erase mode");
			});
			s.on(EVENT_RFID_KNOWN, "erasecommandconfirm").attachAction([this](EventSource& e) {
				vbeeper->playSpecial("shortup");
				rgbled->playColor(LED_ERASE_BLINKFASTCOLOR);
				sendPermanentMessage(String()+"Erasing tag "+dataman->getCurrentTag()+" with ID "+dataman->getCurrentID()+", press to confirm");
			});
			s.on(EVENT_RFID_COMMAND, "erasecommandconfirm").attachAction([this](EventSource& e) {
				vbeeper->playSpecial("shortup");
				rgbled->playColor(LED_ERASE_BLINKFASTCOLOR);
				sendPermanentMessage(String()+"Erasing tag "+dataman->getCurrentTag()+" with Command "+dataman->getCurrentCommand()+", press to confirm");
			});
		}};
		State erasecommandconfirm{"erasecommandconfirm", this, [this](State& s) {
			s.onEnter([this]() {
				//				sendPermanentMessage(String()+APP_NAME+" erasing tag: confirm to erase tag "+dataman->getCurrentTag());
			});
			applyCommonUI(s, {EVENT_BUTTON_PRESS});
			s.on(EVENT_BUTTON_PRESS, "rfiddbsyncing").attachAction([this](EventSource& e) {
				dataman->getRfidDB()->erase(dataman->getCurrentTag());
				vbeeper->playSpecial("superhighbeep");
				rgbled->playColor(LED_ERASE_CONFIRM_COLOR);
				if (dataman->getCurrentCommand().length() > 0) {
					printmessage=String()+"Successfully erased tag "+dataman->getCurrentTag()+" with Command "+dataman->getCurrentCommand()+"!";
				} else {
					printmessage=String()+"Successfully erased tag "+dataman->getCurrentTag()+" with ID "+dataman->getCurrentID()+"!";
				}

			});
		}};

		///////////////////////////////////////////////////
		// UNDO
		State undocommand{"undocommand", this, [this](State& s) {
			applyCommonUI(s, {EVENT_BUTTON_PRESS});
			s.onEnter([this]() {
				vbeeper->playSpecial("bungee");
				rgbled->playColor(LED_UNDO_BLINKCOLOR);
				sendPermanentMessage(String()+"Undo super-tag scanned "+dataman->getCurrentTag()+ ": press button to undo last action on superscale");
			});
			s.on(EVENT_BUTTON_PRESS, "undoexecute").attachAction([this](EventSource& e) {
				vbeeper->playSpecial("shortup");
				rgbled->playColor(LED_UNDO_BLINKFASTCOLOR);
				//	sendPermanentMessage(String()+"Successfully cancelled last action on superscale");
			});
		}};
		State undoexecute{"undoexecute", this, [this](State& s) {
			s.onEnter([this]() {
				dataman->fetchUndo();
			});
			s.on(EVENT_UNDO_SUCCESS, "idle").attachAction([this](EventSource& e) {
				vbeeper->playSpecial("superhighbeep");
				rgbled->playColor(LED_UNDO_CONFIRM_COLOR);
				sendPermanentMessage(String()+"Undo on superscale successful!");
			});
			s.on(EVENT_UNDO_FAIL, "idle").attachAction([this](EventSource& e) {
				briefErrorFB();
			});
		}};

		///////////////////////////////////////////////////
		// RFID UNDO
		bool readyForUndo=false;
		State rfidundocommand{"rfidundocommand", this, [this](State& s) {
			applyCommonUI(s, {EVENT_BUTTON_PRESS, EVENT_RFID_RFIDUNDO});
			s.onEnter([this]() {
				vbeeper->playSpecial("exclamation");
				readyForUndo=false;
				rgbled->playColor(LED_RFIDUNDO_BLINKCOLOR);
				dataman->getRfidDB()->syncRemoteFile();		
		/*		dataman->getRfidDB()->sync([this](bool success, String error) {
					if (success) {
						readyForUndo=false;
						vbeeper->playSpecial("ok");
						String undoDesc = dataman->getRfidDB()->getLastUndoDescription();
						sendPermanentMessage(String()+"RFID undo tag scanned: "+dataman->getCurrentTag()+". Press the button to "+undoDesc+".");
					} else dispatchEventSM(EVENT_SYNC_ERROR); // we need a transition
				});*/
			});
			s.onSelfInternal(EVENT_SYNC_SUCCESS).attachAction([this](EventSource& e) {
				readyForUndo=true;
				vbeeper->playSpecial("ok");
				String undoDesc = dataman->getRfidDB()->getLastUndoDescription();
				sendPermanentMessage(String()+"RFID undo tag scanned: "+dataman->getCurrentTag()+". Press the button to "+undoDesc+".");
			});
			s.on(EVENT_SYNC_ERROR, "idle").attachAction([this](EventSource& e) {
				briefErrorFB("Sync error during rfid-undo operation",1);
			});
			s.on(EVENT_RFID_RFIDUNDO, "idle").attachAction([this](EventSource& e) {
				briefErrorFB("Rfid-Undo super-tag scanned twice, cancelling operation",1);
			});			
			s.on(EVENT_BUTTON_PRESS, "rfidundoexecute").attachAction([this](EventSource& e) {
				vbeeper->playSpecial("shortup");
				rgbled->playColor(LED_RFIDUNDO_BLINKFASTCOLOR);
			}).attachGuard([this](EventSource& e) {
				if (!readyForUndo) {
					briefErrorFB("Not ready for rfid-undo operation");
					return false;
				}
				return true;
			});
		}};
		String undoDesc ;
		String undoResultDesc;
		State rfidundoexecute{"rfidundoexecute", this, [this](State& s) {
			s.onEnter([this]() {
				undoDesc = dataman->getRfidDB()->getLastUndoDescription();
				undoResultDesc = dataman->getRfidDB()->getLastUndoResultDescription();
				Serial.println(String()+"State machine: Executing rfid-undo for operation: '"+undoDesc+"'");
				dataman->fetchRfidUndo();
			});
			s.on(EVENT_RFIDUNDO_SUCCESS, "idle").attachAction([this](EventSource& e) {	
				vbeeper->playSpecial("superhighbeep");
				rgbled->playColor(LED_RFIDUNDO_COLOR);
				sendPermanentMessage(String()+"RFID undo successful: "+undoResultDesc+".");
				Serial.println(String()+"State machine: Rfid-undo successful for operation: '"+undoDesc+"'");
			});
			s.on(EVENT_RFIDUNDO_FAIL, "idle").attachAction([this](EventSource& e) {
				String message=String()+"State machine: Rfid-undo failed for operation: '"+undoDesc+"'";
				briefErrorFB(message);
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




void processQueueSM() { // The callback that pulls from the queue
	while (!evtQueue.empty()) {
		rsm.dispatch(evtQueue.front()); // 1. Execute the oldest event
		evtQueue.pop();                 // 2. Remove it from the queue
	}
}

void dispatchEventSM(String eventname) {
	evtQueue.push(eventname);
}





#endif
