#ifndef RGBLED_H
#define RGBLED_H

#include <Arduino.h>

#define MAX_QUEUE 24 // Increased to handle multi-step commands

enum LedMode { MODE_IDLE, MODE_FADING, MODE_BLINKING };
enum BlinkPhase { PHASE_ENTRANCE, PHASE_TO_A, PHASE_TO_B };

struct LedOrder {
	bool isBlink;
	bool isTemp;    // Is this a temporary interruption?
	bool isReturn;  // Is this the command to return to normal?
	uint8_t r1, g1, b1;
	uint8_t r2, g2, b2;
	unsigned long duration;
	unsigned long entranceDuration; // Special duration used when smoothly returning to a pulse
};

class RGBLed {
private:
	uint8_t pinR, pinG, pinB;
	uint8_t curR, curG, curB;
	uint8_t startR, startG, startB;

	LedMode currentMode = MODE_IDLE;
	BlinkPhase bPhase = PHASE_ENTRANCE;

	LedOrder activeOrder;
	LedOrder lastNormalOrder = {false, false, false, 0,0,0, 0,0,0, 0, 0}; // Memory of the background state

	unsigned long startTime;
	LedOrder queue[MAX_QUEUE];
	int head = 0, tail = 0, count = 0;

	// Internal helper to push to queue
	bool _push(bool blink, bool temp, bool isRet, uint8_t r1, uint8_t g1, uint8_t b1, uint8_t r2, uint8_t g2, uint8_t b2, unsigned long dur) {
		if (count >= MAX_QUEUE) return false;
		queue[head] = {blink, temp, isRet, r1, g1, b1, r2, g2, b2, dur, dur};
		head = (head + 1) % MAX_QUEUE;
		count++;
		return true;
	}

public:
	RGBLed(uint8_t r, uint8_t g, uint8_t b) : pinR(r), pinG(g), pinB(b) {}

	void begin() {
		// Serial.println(String()+"RGBLed:: begin:");
		ledcAttach(pinR, 5000, 8);
		ledcAttach(pinG, 5000, 8);
		ledcAttach(pinB, 5000, 8);
		setDirect(0, 0, 0);
	}

	// Standard Fade (Remembered as Permanent)
	bool fadeTo(uint8_t r, uint8_t g, uint8_t b, unsigned long ms) {
		// Serial.println(String()+"RGBLed:: fadeTo:");
		return _push(false, false, false, r, g, b, 0, 0, 0, ms);
	}

	// Oscillating Pulse (Remembered as Permanent)
	bool pulse(uint8_t r1, uint8_t g1, uint8_t b1, uint8_t r2, uint8_t g2, uint8_t b2, unsigned long msPerTransition) {
		// Serial.println(String()+"RGBLed:: pulse:");
		return _push(true, false, false, r1, g1, b1, r2, g2, b2, msPerTransition);
	}

	/**
	 * Fades to a color, stays there, then smoothly returns to the PREVIOUS permanent state.
	 */
	void tempFade(uint8_t r, uint8_t g, uint8_t b, unsigned long fadeTime, unsigned long holdTime) {
		// Serial.println(String()+"RGBLed:: tempFade:");

		// 1. Transition IN (Temporary)
		_push(false, true, false, r, g, b, 0, 0, 0, fadeTime);
		// 2. HOLD (Temporary)
		_push(false, true, false, r, g, b, 0, 0, 0, holdTime);
		// 3. RETURN COMMAND (Tells the queue to restore the last normal state)
		_push(false, true, true, 0, 0, 0, 0, 0, 0, fadeTime);
	}

	void setDirect(uint8_t r, uint8_t g, uint8_t b) {
		count = 0; head = 0; tail = 0;
		currentMode = MODE_IDLE;
		lastNormalOrder = {false, false, false, r, g, b, 0, 0, 0, 0, 0};
		writeRGB(r, g, b);
	}

	void update() {
		if ((currentMode == MODE_IDLE && count > 0) || (currentMode == MODE_BLINKING && count > 0)) {
			startNextOrder();
		}
		if (currentMode == MODE_FADING) handleFade();
		else if (currentMode == MODE_BLINKING) handlePulse();
	}

private:
	void startNextOrder() {
		LedOrder next = queue[tail];
		tail = (tail + 1) % MAX_QUEUE;
		count--;

		// If this is a normal order, commit it to memory
		if (!next.isTemp) {
			lastNormalOrder = next;
		}

		// If this is a return command, load the memory
		if (next.isReturn) {
			unsigned long returnFadeTime = next.duration;
			next = lastNormalOrder;
			next.entranceDuration = returnFadeTime; // Use the tempFade's transition time for a smooth entry
		}

		activeOrder = next;
		startR = curR; startG = curG; startB = curB;
		startTime = millis();

		if (activeOrder.isBlink) {
			currentMode = MODE_BLINKING;
			bPhase = PHASE_ENTRANCE;
		} else {
			currentMode = MODE_FADING;
		}
	}

	void interpolate(uint8_t sR, uint8_t sG, uint8_t sB, uint8_t tR, uint8_t tG, uint8_t tB, unsigned long dur) {
		unsigned long elapsed = millis() - startTime;
		float progress = (dur == 0) ? 1.0f : (float)elapsed / (float)dur;
		if (progress > 1.0f) progress = 1.0f;
		writeRGB(
				sR + (int16_t(tR - sR) * progress),
				sG + (int16_t(tG - sG) * progress),
				sB + (int16_t(tB - sB) * progress)
		);
	}

	void handleFade() {
		unsigned long dur = activeOrder.entranceDuration;
		interpolate(startR, startG, startB, activeOrder.r1, activeOrder.g1, activeOrder.b1, dur);
		if (millis() - startTime >= dur) {
			currentMode = MODE_IDLE;
		}
	}

	void handlePulse() {
		// Entrance can have a custom duration (for smooth returns), but loops use normal duration
		unsigned long dur = (bPhase == PHASE_ENTRANCE) ? activeOrder.entranceDuration : activeOrder.duration;

		switch (bPhase) {
		case PHASE_ENTRANCE:
			interpolate(startR, startG, startB, activeOrder.r1, activeOrder.g1, activeOrder.b1, dur);
			if (millis() - startTime >= dur) { bPhase = PHASE_TO_B; startTime = millis(); }
			break;
		case PHASE_TO_B:
			interpolate(activeOrder.r1, activeOrder.g1, activeOrder.b1, activeOrder.r2, activeOrder.g2, activeOrder.b2, dur);
			if (millis() - startTime >= dur) { bPhase = PHASE_TO_A; startTime = millis(); }
			break;
		case PHASE_TO_A:
			interpolate(activeOrder.r2, activeOrder.g2, activeOrder.b2, activeOrder.r1, activeOrder.g1, activeOrder.b1, dur);
			if (millis() - startTime >= dur) { bPhase = PHASE_TO_B; startTime = millis(); }
			break;
		}
	}

	void writeRGB(uint8_t r, uint8_t g, uint8_t b) {
//		Serial.println(String()+"RGBLed:: writeRGB:"+r+", "+g+","+b);
		curR = r; curG = g; curB = b;
		ledcWrite(pinR, r); ledcWrite(pinG, g); ledcWrite(pinB, b);
	}

public:
	/**
	 * Fades to a temp color, holds it, then fades to a NEW solid base color.
	 */
	void tempThenFade(uint8_t tr, uint8_t tg, uint8_t tb, unsigned long fadeTime, unsigned long holdTime,
			uint8_t br, uint8_t bg, uint8_t bb) {
		fadeTo(tr, tg, tb, fadeTime);     // 1. Fade to temp color
		fadeTo(tr, tg, tb, holdTime);     // 2. Hold temp color
		fadeTo(br, bg, bb, fadeTime);     // 3. Fade to NEW base color (Overwrites memory!)
	}

	/**
	 * Fades to a temp color, holds it, then starts a NEW looping pulse.
	 */
	void tempThenPulse(uint8_t tr, uint8_t tg, uint8_t tb, unsigned long fadeTime, unsigned long holdTime,
			uint8_t pr1, uint8_t pg1, uint8_t pb1, uint8_t pr2, uint8_t pg2, uint8_t pb2, unsigned long pulseTime) {
		fadeTo(tr, tg, tb, fadeTime);     // 1. Fade to temp color
		fadeTo(tr, tg, tb, holdTime);     // 2. Hold temp color
		pulse(pr1, pg1, pb1, pr2, pg2, pb2, pulseTime); // 3. Pulse NEW base (Overwrites memory!)
	}

	void playColor(String color){
 		Serial.println(String()+"RGBLed:: playColor:"+color);

		// Permanent Base States
		if(color=="red") fadeTo(255,0,0,1000);
		if(color=="green") fadeTo(0,255,0,1000);
		if(color=="blue") fadeTo(0,0,255,1000);
		if(color=="yellow") fadeTo(255,255,0,1000);
		if(color=="yellowish") fadeTo(255,128,0,1000);
		if(color=="yellowishblue") fadeTo(255,128,64,1000);
		if(color=="white") fadeTo(255,255,255,1000);
		if(color=="orange") fadeTo(255,165,0,1000);
		if(color=="purple") fadeTo(255,0,255,1000);
		if(color=="cyan") fadeTo(0,255,255,1000);

		// Permanent Pulse States
		if(color=="greenbluepulse") pulse(0, 255, 255, 0, 255,0, 500);
		if(color=="fastwhitebluepulse") pulse(255, 255, 255, 0, 255,0, 100);

		if(color=="bluegreen") pulse(0, 0, 255, 0, 255,0, 2000);
		if(color=="redpulse") pulse(0, 0, 0, 255, 0, 0, 500);
		if(color=="bluered") pulse(0, 0, 255, 255, 0,0, 500);
		if(color=="fastredpulse") pulse(0, 0, 0, 255, 0, 0, 200);
		if(color=="redbluepulse") pulse(0, 0, 255, 255, 0, 0, 200);
		if(color=="yellowpulse") pulse(0, 0, 0, 255, 255, 0, 500);
		if(color=="fastyellowpulse") pulse(0, 0, 0, 255, 255, 0, 100);

		if(color=="whitepulse") pulse(0, 0, 0, 255, 255, 255, 500);
		if(color=="orangepulse") pulse(0, 0, 0, 255, 165, 0, 500);
		if(color=="purplepulse") pulse(0, 0, 0, 255, 0, 255, 500);
		if(color=="fastpurplepulse") pulse(0, 0, 0, 255, 0, 255, 100);
		if(color=="cyanpulse") pulse(0, 0, 0, 0, 255, 255, 500);
		if(color=="bluishcyanpulse") pulse(0, 0, 0, 0, 128, 255, 500);
		if(color=="fastcyanpulse") pulse(0, 0, 0, 0, 255, 255, 200);
		if(color=="bluepulse") pulse(0, 0, 0, 0, 255, 255, 200);

		// Temporary Interruptions
		if(color=="tempocyan") tempFade(0, 255, 255, 300, 800);
		if(color=="tempoblue") tempFade(0, 0, 255, 300, 800);
		if(color=="tempored") tempFade(255, 0, 0, 300, 800);
		if(color=="tempoyellow") tempFade(255, 255, 0, 300, 800);
		if(color=="tempoyellowish") tempFade(255, 128, 0, 300, 800);
		if(color=="tempowhite") tempFade(255, 255, 255, 300, 800);
		if(color=="tempoorange") tempFade(255, 165, 0, 300, 800);
		if(color=="tempopurple") tempFade(255, 0, 255, 300, 800);

		if(color=="tempobluethenred") tempThenFade(0, 0, 255, 200, 1000, 255, 0, 0);// Temp Blue (200ms fade, 1000ms hold) -> Becomes permanent Red
		if(color=="tempoyellowthengreen") tempThenFade(255, 255, 0, 200, 1000, 0, 255, 0);
		if(color=="tempobluethengreen") tempThenFade(0, 0, 255, 200, 1000, 0, 255, 0);
		if(color=="tempowhitethengreen") tempThenFade(0, 0, 255, 200, 1000, 0, 255, 0);

		if(color=="tempobluethenpulse") tempThenPulse(0, 0, 255, 200, 1000, 255, 0, 0, 255, 255, 255, 500);// Temp Blue (200ms fade, 1000ms hold) -> Becomes Red/White pulse

	}
};


// --- USAGE ---
/*
RGBLed myLED(27, 14, 12);

void setup() {
  myLED.begin();

  // 1. Start with a dim green ambient light
  myLED.fadeTo(0, 50, 0, 1000);

  // 2. Perform a "Flash" notification (White)
  // It will:
  // - Fade Green -> White (200ms)
  // - Stay White (1000ms)
  // - Fade White -> Green (200ms)
  myLED.tempFade(255, 255, 255, 200, 1000);

  // 3. Queue a pulse afterwards just to show succession works
  myLED.pulse(0, 0, 255, 0, 0, 50, 2000);
}

void loop() {
  myLED.update();
}*/

#endif
