#ifndef RESETDETECTOR_H
#define RESETDETECTOR_H

#include <Arduino.h>
#include <Preferences.h>
#include <Ticker.h>

// ==============================================================================
// BASE CLASS: Multiple Click Detector
// ==============================================================================
class MultiClickDetector {
protected:
    uint32_t count = 0;
    uint32_t timeout_ms;
    Ticker ticker;
   
    virtual void expire() {count = 0;} // Virtual so the derived class can inject NVS clearing
public:
    MultiClickDetector(uint32_t timeout) : timeout_ms(timeout) {}
    // Call on hardware click: increments and starts/restarts the expiration timeout
    void click() {
        count++;
        ticker.once_ms(timeout_ms, [this]() { this->expire(); });
    }
    // Call at any moment to see how many clicks we got so far
    uint32_t getCount() const {return count;}
};

// ==============================================================================
// DERIVED CLASS: Multiple Reset Press Detector
// ==============================================================================
class ResetDetector : public MultiClickDetector {
private:
    Preferences prefs;

protected:
    // When timeout expires, clear RAM (via base) and wipe NVS automatically
    void expire() override {
        MultiClickDetector::expire(); // Resets 'count' to 0
        prefs.begin("FastPress", false);
        prefs.putUInt("rstCnt", 0); 
        prefs.end();
        Serial.println("ResetDetector::expired counter: " + String(count));
    }

public:
    ResetDetector(uint32_t timeout) : MultiClickDetector(timeout) {}

    void resetCounter(){
        prefs.begin("FastPress", false);
        prefs.putUInt("rstCnt", 0);
        prefs.end();
        Serial.println("Reset counter cleared");
    }
    // Moved initialization logic out of the Constructor and into begin()
    void begin() {
        prefs.begin("FastPress", false);
        count = prefs.getUInt("rstCnt", 0) + 1;// Increment counter from NVS
        prefs.putUInt("rstCnt", count);// Save it back
        prefs.end();
        uint32_t current_millis = millis();
        Serial.println("ResetDetector::loaded counter: " + String(count)+" milli:"+current_millis);

        if (timeout_ms > current_millis) {
            ticker.once_ms(timeout_ms - current_millis, [this]() { this->expire(); });
        } else {
            this->expire(); // Already expired during boot
        }
    }
};

#endif
