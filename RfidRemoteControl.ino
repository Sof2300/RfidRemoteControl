/*
	Test&fix
		- test more set & save
		- test more generateid & save
		- test set remote command
		- test attach command to tag
		- test erase tag
 */
#define ESP32BUILD
// Supported platforms ESP32
#include <Ticker.h>

#include <mylib.h>
#include <device/RFIDReader.h>
#include <device/Beeper.H>
#include <FS/SimpleFS.H>
#include <datastruct/GenString.H>

#include "WifiManager.h"
#include <Button2.h>

#include "RGBLed.h"

#include "RfidStateMachine.h"


#define SAVEBUTTON_PIN 15//36
#define LED_BUILTIN 2

// Others pins are 3v3, Gnd, SDA/SS=5, SCK=18, MOSI=23, MISO=19, RST=21, IRQ=17
#define SS_PIN          5
#define RST_PIN         0
#define IRQ_PIN         17

//#define LED_PIN LED_BUILTIN
#define BUZZER_PIN 4

#define LED_RED_PIN 27
#define LED_GREEN_PIN 14
#define LED_BLUE_PIN 12

#define CONFIGFILE "config.json"
#define RFIDFILE "RFID2ID.csv"


#include "DataManager.h"

RGBLed rgbled(LED_RED_PIN, LED_GREEN_PIN, LED_BLUE_PIN);

Button2 savebutton = Button2(SAVEBUTTON_PIN);//,INPUT_PULLUP,true);

VBeeper vbeeper;
RFIDReader reader;

DataManager dataman;



#define BUTTONPAUSEMS 500
unsigned long buttonts=0;
void buttonsave(Button2& btn) {
	//if(!wificonnected) return;
	Serial.println("Save button pressed!");
	if(millis()-buttonts<BUTTONPAUSEMS) {Serial.println("but in short succession to another button press");return;}
	buttonts=millis();
	//dataman.triggerSave();
	dispatchEventSM(EVENT_BUTTON_PRESS);
}

void released(Button2& btn) {
    Serial.print("released: ");
    Serial.println(btn.wasPressedFor());
}
void changed(Button2& btn) {
    Serial.println("changed");
}
void tap(Button2& btn) {
    Serial.println("tap");
}

void setup() {
	Serial.begin(115200); // Initialize serial communications with the PC
	//while (!Serial);      // Do nothing if no serial port is opened (added for Arduinos based on ATMEGA32U4)

	//	ledcAttach(LED_PIN, LEDC_BASE_FREQ, LEDC_TIMER_12_BIT);//	ledcFadeWithInterrupt(LED_PIN, 0, 4095, 1000, ledFadeEnded);
	//	ledexpress.flashPattern("accelerating pulse",1000);
	//	ledexpress.flashPattern("wakeup",3000,"Expo");
	//	rgbled.fadeTo(255, 255, 255, 1000);

	rgbled.begin();
	vbeeper.init(BUZZER_PIN);	// init vbeeper
//	vbeeper.playSpecial("highbeep");
//	delay(5000);

	SIMPLEFS.begin();
	SIMPLEFS.printInfo();

 	initSM(&rgbled,&vbeeper, &dataman);

	// then load the distant file from superscale too
   	dataman.init(CONFIGFILE,RFIDFILE);

	String ssid=dataman.getConfig(SSID_KEY);
	String password=dataman.getConfig(PASSWORD_KEY);
 	connectWifi(ssid,password);

 	reader.init(SS_PIN,RST_PIN,IRQ_PIN);
	//savebutton.setDebounceTime(30);
//	savebutton.setPressedState(LOW);
 	savebutton.setPressedHandler(buttonsave);
/*
savebutton.setChangedHandler(changed);
savebutton.setReleasedHandler(released);
savebutton.setTapHandler(tap);
*/
	Serial.println(String()+"End setup in "+millis()+" ms");
}

// 65mA after setup, 18.8mA in deep sleep, 150mA peak on start up 110mA on read tag

/**
 * Main loop.
 */
unsigned ts=0;


void loop() {
	rgbled.update();
  	dataman.update();
 	savebutton.loop();
	checkWifi();
	//reader.mfrc522_fast_Reset();
 	String tag =reader.readCard();
	if(!tag.isEmpty()) {
	 	dataman.tagFound(tag);
		//	dataman.foundTag(tag);
		//		tagwaiting=tag;
		ts=0;
	} else dataman.noTagFound();

 	//ts=millis();
	delay(50);

}

bool wificonnectedprev=false, connectionfailedprev=false, connectingprev=false;
void checkWifi(){
	loopWifi();
	if(connecting && !connectingprev) {
		dispatchEventSM(EVENT_WIFI_CONNECTING);
	}
	connectingprev=connecting;
	if(connectionfailed && !connectionfailedprev) {
		dispatchEventSM(EVENT_WIFI_ERROR);
	}
	connectionfailedprev=connectionfailed;
	if(wificonnected && !wificonnectedprev) {
		dispatchEventSM(EVENT_WIFI_CONNECTED);
	}

	if (!wificonnected && wificonnectedprev) {
		dispatchEventSM(EVENT_WIFI_LOST);
	}

	wificonnectedprev=wificonnected;
}
