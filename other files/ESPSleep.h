#ifndef ESPSLEEP_H
#define ESPSLEEP_H

#if CONFIG_IDF_TARGET_ESP32
#define THRESHOLD 40   /* Greater the value, more the sensitivity */
#else                  //ESP32-S2 and ESP32-S3 + default for other chips (to be adjusted) */
#define THRESHOLD 5000 /* Lower the value, more the sensitivity */
#endif
#define uS_TO_S_FACTOR 1000000ULL /* Conversion factor for micro seconds to seconds */
#define uS_TO_MS_FACTOR 1000ULL /* Conversion factor for micro seconds to millis seconds */


RTC_DATA_ATTR int bootCount = 0;

class EspSleep {
	String wakeup_reason() {
		esp_sleep_wakeup_cause_t wakeup_reason;

		wakeup_reason = esp_sleep_get_wakeup_cause();

		switch (wakeup_reason) {
		case ESP_SLEEP_WAKEUP_EXT0:     return "Wakeup caused by external signal using RTC_IO";
		case ESP_SLEEP_WAKEUP_EXT1:     return "Wakeup caused by external signal using RTC_CNTL";
		case ESP_SLEEP_WAKEUP_TIMER:    return "Wakeup caused by timer";
		case ESP_SLEEP_WAKEUP_TOUCHPAD: return "Wakeup caused by touchpad";
		case ESP_SLEEP_WAKEUP_ULP:      return "Wakeup caused by ULP program";
		default:                        return String()+"Wakeup was not caused by deep sleep: "+ wakeup_reason;
		}

	}
public:
	void touchWakeUp(byte pin, unsigned threshold){touchSleepWakeUpEnable(pin, threshold);}
	void timerWakeup(unsigned timetosleep){esp_sleep_enable_timer_wakeup(timetosleep * uS_TO_S_FACTOR);}

	void deepSleepNow(){
		++bootCount;
		Serial.println(String()+"ESPSleep: Boot number: " + String(bootCount)+", millis:"+millis());
		Serial.println(wakeup_reason());
		Serial.println("ESPSleep: Going to deep sleep now-----------------");
		Serial.flush();
		esp_deep_sleep_start();
	}
	void lightSleepNow(){
			++bootCount;
			Serial.println(String()+"ESPSleep: Boot number: " + String(bootCount)+", millis:"+millis());
			Serial.println(wakeup_reason());
			Serial.println("ESPSleep: Going to light sleep now-----------------");
			Serial.flush();

			esp_light_sleep_start();
		}
} ESPSleep;




#endif
