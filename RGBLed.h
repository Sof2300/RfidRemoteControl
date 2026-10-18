#ifndef RGBLED_H
#define RGBLED_H

#include <vector>

#include <Arduino.h>
#include <Ticker.h>


#define MAX_QUEUE 24
#define LEDTICKERMS 5

#define SLOWLED 582
#define SPEEDLED 290
#define FASTLED 145
#define ULTRALED 36

#define SLOWFADE_DURATION 776
#define SLOWHOLD_DURATION 388
#define FADE_DURATION 388
#define HOLD_DURATION 194
#define FASTFADE_DURATION 194
#define FASTHOLD_DURATION 97
#define SUPERFASTFADE_DURATION 97
#define SUPERFASTHOLD_DURATION 48
//#define ULTRAFADE_DURATION 24
//#define ULTRAHOLD_DURATION 12
#define ULTRAFADE_DURATION 12
#define ULTRAHOLD_DURATION 6
#define LIGHTNINGFADE_DURATION 12
#define LIGHTNINGHOLD_DURATION 6

#define SLOWPULSE_DURATION 2327
#define PULSE_DURATION 582
#define FASTPULSE_DURATION 194
#define SUPERFASTPULSE_DURATION 97

#define FADETO_DURATION 1164
#define TEMP_DURATION 776
#define TEMPFADEFADE_DURATION 194
#define TEMPFADEHOLD_DURATION 1164
#define TEMPPULSEFADE_DURATION 194
#define TEMPPULSEHOLD_DURATION 1164



// Sequence interrupt policy options:
#define SEQ_INTERRUPT_IMMEDIATE 0 // Interrupt sequence immediately on the fly (smoothly fades from current intermediate color)
#define SEQ_INTERRUPT_STEP      1 // Finish current color transition + pause before starting queued order
#define SEQ_INTERRUPT_CYCLE     2 // Finish entire sequence cycle (all colors) before starting queued order

// Default interrupt mode (can be overridden before including or in Definitions.h)
#ifndef SEQ_INTERRUPT_MODE
#define SEQ_INTERRUPT_MODE SEQ_INTERRUPT_CYCLE
#endif

enum SeqInterruptMode {
    SEQ_MODE_IMMEDIATE = SEQ_INTERRUPT_IMMEDIATE,
    SEQ_MODE_STEP      = SEQ_INTERRUPT_STEP,
    SEQ_MODE_CYCLE     = SEQ_INTERRUPT_CYCLE
};

enum LedMode { MODE_IDLE, MODE_FADING, MODE_BLINKING, MODE_SEQUENCE };
enum BlinkPhase { PHASE_ENTRANCE, PHASE_TO_A, PHASE_TO_B };

struct LedOrder {
    bool isBlink;
    bool isTemp;
    bool isReturn;
    uint8_t r1, g1, b1;
    uint8_t r2, g2, b2;
    unsigned long duration;
    unsigned long entranceDuration;
};

// Pure color representation (decoupled from timing)
struct RGBColor {
    uint8_t r, g, b;
};

// Backward compatibility alias & wrapper
using RGB = RGBColor;
struct ColorStep {
    uint8_t r, g, b;
    unsigned long holdDuration;
    ColorStep(uint8_t r0 = 0, uint8_t g0 = 0, uint8_t b0 = 0, unsigned long hold0 = 0)
        : r(r0), g(g0), b(b0), holdDuration(hold0) {}
    operator RGBColor() const { return {r, g, b}; }
};

// Pure color palettes (timing is configured when playing, not baked into data)
static const std::vector<RGBColor> RAINBOW_PALETTE = {
    {255, 0, 0},       // Red (0°)
    {255, 127, 0},     // Orange (30°)
    {255, 255, 0},     // Yellow (60°)
    {0, 255, 0},       // Green (120°)
    {0, 0, 255},       // Blue (240°)
    {148, 0, 255},     // Violet (275°)
    {255, 0, 255}      // Magenta (300°)
};

static const std::vector<RGBColor> MAGENTA_RAINBOW_PALETTE = {
    {255, 0, 255},     // Magenta (300°)
    {255, 0, 0},       // Red (0°)
    {255, 127, 0},     // Orange (30°)
    {255, 255, 0},     // Yellow (60°)
    {0, 255, 0},       // Green (120°)
    {0, 0, 255}        // Blue (240°)
};

// Safe rainbow without red and orange (used for errors)
static const std::vector<RGBColor> SAFE_RAINBOW_PALETTE = {
    {255, 255, 0},     // Yellow (60°)
    {0, 255, 0},       // Green (120°)
    {0, 0, 255},       // Blue (240°)
    {148, 0, 255},     // Violet (275°)
    {255, 0, 255}      // Magenta (300°)
};

static const std::vector<RGBColor> SAFE_MAGENTA_RAINBOW_PALETTE = {
    {255, 0, 255},     // Magenta (300°)
    {255, 255, 0},     // Yellow (60°)
    {0, 255, 0},       // Green (120°)
    {0, 0, 255}        // Blue (240°)
};

// Generic ping-pong palettes (RGB and CMY for library use)
static const std::vector<RGBColor> RGB_PINGPONG_PALETTE = {
    {255, 0, 0},       // Red (A)
    {0, 255, 0},       // Green (B)
    {0, 0, 255},       // Blue (C)
    {0, 255, 0}        // Green (B) - creates A->B->C->B->A loop
};

static const std::vector<RGBColor> CMY_PINGPONG_PALETTE = {
    {0, 255, 255},     // Cyan (A)
    {255, 0, 255},     // Magenta (B)
    {255, 255, 0},     // Yellow (C)
    {255, 0, 255}      // Magenta (B) - creates A->B->C->B->A loop
};

#define RAINBOW_STEPS RAINBOW_PALETTE
#define MAGENTA_RAINBOW_STEPS MAGENTA_RAINBOW_PALETTE

struct SequenceData {
    std::vector<RGBColor> colors;
    unsigned long transitionDuration;
    unsigned long pauseDuration;
    bool loop;
    bool temp;
    bool inEntrance;
    uint8_t entranceStartR, entranceStartG, entranceStartB;
    size_t currentStep;
    size_t stepsCompleted;
    unsigned long stepStartTime;
    bool isPausing;
    unsigned long pauseStartTime;
};

class RGBLed {
private:
    uint8_t pinR, pinG, pinB;
    uint8_t curR = 0, curG = 0, curB = 0;
    uint8_t startR = 0, startG = 0, startB = 0;

    LedMode currentMode = MODE_IDLE;
    BlinkPhase bPhase = PHASE_ENTRANCE;
    SeqInterruptMode seqInterruptMode = (SeqInterruptMode)SEQ_INTERRUPT_MODE;

    LedOrder activeOrder;
    LedOrder lastNormalOrder = {false, false, false, 0, 0, 0, 0, 0, 0, 0, 0};

    unsigned long startTime = 0;
    LedOrder queue[MAX_QUEUE];
    int head = 0, tail = 0, count = 0;

    Ticker updateTicker;
    bool tickerAttached = false;

    SequenceData currentSequence;

    bool _push(bool blink, bool temp, bool isRet, uint8_t r1, uint8_t g1, uint8_t b1, uint8_t r2, uint8_t g2, uint8_t b2, unsigned long dur) {
        if (count >= MAX_QUEUE) return false;
        queue[head] = {blink, temp, isRet, r1, g1, b1, r2, g2, b2, dur, dur};
        head = (head + 1) % MAX_QUEUE;
        count++;
        return true;
    }

    static inline uint8_t lerpChannel(uint8_t a, uint8_t b, float t) {
        return (uint8_t)(a + (int16_t(b - a) * t));
    }

    void writeInterpolated(uint8_t r1, uint8_t g1, uint8_t b1, uint8_t r2, uint8_t g2, uint8_t b2, float progress) {
        if (progress < 0.0f) progress = 0.0f;
        if (progress > 1.0f) progress = 1.0f;
        writeRGB(
            lerpChannel(r1, r2, progress),
            lerpChannel(g1, g2, progress),
            lerpChannel(b1, b2, progress)
        );
    }

    bool shouldExitSequence() {
        if (!currentSequence.loop && currentSequence.stepsCompleted >= currentSequence.colors.size()) {
            return true;
        }
        if (count > 0) {
            if (seqInterruptMode == SEQ_MODE_STEP) {
                return true;
            }
            if (seqInterruptMode == SEQ_MODE_CYCLE) {
                size_t numColors = currentSequence.colors.size();
                return (currentSequence.stepsCompleted >= numColors) && (currentSequence.stepsCompleted % numColors == 0);
            }
        }
        return false;
    }

public:
    RGBLed(uint8_t r, uint8_t g, uint8_t b) : pinR(r), pinG(g), pinB(b) {}

    ~RGBLed() {
        if (tickerAttached) {
            updateTicker.detach();
            tickerAttached = false;
        }
    }

    void setSequenceInterruptMode(SeqInterruptMode mode) {
        seqInterruptMode = mode;
    }

    SeqInterruptMode getSequenceInterruptMode() const {
        return seqInterruptMode;
    }

    void begin() {
        pinMode(pinR, OUTPUT);
        pinMode(pinG, OUTPUT);
        pinMode(pinB, OUTPUT);

        ledcAttach(pinR, 5000, 8);
        ledcAttach(pinG, 5000, 8);
        ledcAttach(pinB, 5000, 8);

        setDirect(0, 0, 0);

        // Start ticker for automatic updates (10ms interval)
        if (!tickerAttached) {
            updateTicker.attach_ms(LEDTICKERMS, [this]() { this->update(); });
            tickerAttached = true;
        }
    }

    bool fadeTo(uint8_t r, uint8_t g, uint8_t b, unsigned long ms) {
        return _push(false, false, false, r, g, b, 0, 0, 0, ms);
    }

    bool pulse(uint8_t r1, uint8_t g1, uint8_t b1, uint8_t r2, uint8_t g2, uint8_t b2, unsigned long msPerTransition) {
        return _push(true, false, false, r1, g1, b1, r2, g2, b2, msPerTransition);
    }

    void tempFade(uint8_t r, uint8_t g, uint8_t b, unsigned long fadeTime, unsigned long holdTime) {
        _push(false, true, false, r, g, b, 0, 0, 0, fadeTime);
        _push(false, true, false, r, g, b, 0, 0, 0, holdTime);
        _push(false, true, true, 0, 0, 0, 0, 0, 0, fadeTime);
    }

    void tempThenFade(uint8_t tr, uint8_t tg, uint8_t tb, unsigned long fadeTime, unsigned long holdTime, uint8_t br, uint8_t bg, uint8_t bb) {
        fadeTo(tr, tg, tb, fadeTime);
        fadeTo(tr, tg, tb, holdTime);
        fadeTo(br, bg, bb, fadeTime);
    }

    void tempThenPulse(uint8_t tr, uint8_t tg, uint8_t tb, unsigned long fadeTime, unsigned long holdTime,
            uint8_t pr1, uint8_t pg1, uint8_t pb1, uint8_t pr2, uint8_t pg2, uint8_t pb2, unsigned long pulseTime) {
        fadeTo(tr, tg, tb, fadeTime);
        fadeTo(tr, tg, tb, holdTime);
        pulse(pr1, pg1, pb1, pr2, pg2, pb2, pulseTime);
    }

    void setDirect(uint8_t r, uint8_t g, uint8_t b) {
        count = 0; head = 0; tail = 0;
        currentMode = MODE_IDLE;
        currentSequence.colors.clear();
        lastNormalOrder = {false, false, false, r, g, b, 0, 0, 0, 0, 0};
        writeRGB(r, g, b);
    }

    /**
     * Plays a sequence of colors with flexible transition and pause times.
     * @param colors Vector of RGBColor values.
     * @param transitionDuration Milliseconds spent smoothly fading into each color.
     * @param pauseDuration Milliseconds spent holding each color before fading to the next.
     * @param loop If true, continuously loops through the palette.
     * @param temp If true, returns to previous normal state when finished.
     */
    void playSequence(const std::vector<RGBColor>& colors, unsigned long transitionDuration = 200, unsigned long pauseDuration = 100, bool loop = true, bool temp = false) {
        if (colors.empty()) return;
        currentSequence.colors = colors;
        currentSequence.transitionDuration = transitionDuration;
        currentSequence.pauseDuration = pauseDuration;
        currentSequence.loop = loop;
        currentSequence.temp = temp;

        // Smooth entrance: smoothly fade from current LED output into colors[0]
        currentSequence.inEntrance = true;
        currentSequence.entranceStartR = curR;
        currentSequence.entranceStartG = curG;
        currentSequence.entranceStartB = curB;

        currentSequence.currentStep = 0;
        currentSequence.stepsCompleted = 0;
        currentSequence.stepStartTime = millis();
        currentSequence.isPausing = false;
        currentSequence.pauseStartTime = 0;

        currentMode = MODE_SEQUENCE;

        if (!temp && !colors.empty()) {
            lastNormalOrder = {false, false, false, colors[0].r, colors[0].g, colors[0].b, 0, 0, 0, transitionDuration, transitionDuration};
        }
    }

    // Overload for backward compatibility with std::vector<ColorStep>
    void playSequence(const std::vector<ColorStep>& steps, unsigned long transitionDuration = 200, unsigned long pauseDuration = 100, bool loop = true, bool temp = false) {
        std::vector<RGBColor> colors;
        colors.reserve(steps.size());
        for (const auto& s : steps) colors.push_back({s.r, s.g, s.b});
        playSequence(colors, transitionDuration, pauseDuration, loop, temp);
    }

    /**
     * Dynamically adjusts the transition and pause speeds of the active sequence on the fly.
     */
    void setSequenceSpeed(unsigned long transitionDuration, unsigned long pauseDuration) {
        currentSequence.transitionDuration = transitionDuration;
        currentSequence.pauseDuration = pauseDuration;
    }

    /**
     * Plays rainbow with customizable transition and pause times.
     * E.g. playRainbow(200, 100) -> 200ms fade + 100ms pause at each color.
     */
    void playRainbow(unsigned long transitionDuration = 200, unsigned long pauseDuration = 100, bool loop = true, bool temp = false) {
        playSequence(RAINBOW_PALETTE, transitionDuration, pauseDuration, loop, temp);
    }

    void playMagentaRainbow(unsigned long transitionDuration = 200, unsigned long pauseDuration = 100, bool loop = true, bool temp = false) {
        playSequence(MAGENTA_RAINBOW_PALETTE, transitionDuration, pauseDuration, loop, temp);
    }

    /**
     * Plays safe rainbow without red and orange (avoids error color confusion).
     * E.g. playSafeRainbow(200, 100) -> 200ms fade + 100ms pause at each color.
     */
    void playSafeRainbow(unsigned long transitionDuration = 200, unsigned long pauseDuration = 100, bool loop = true, bool temp = false) {
        playSequence(SAFE_RAINBOW_PALETTE, transitionDuration, pauseDuration, loop, temp);
    }

    void playSafeMagentaRainbow(unsigned long transitionDuration = 200, unsigned long pauseDuration = 100, bool loop = true, bool temp = false) {
        playSequence(SAFE_MAGENTA_RAINBOW_PALETTE, transitionDuration, pauseDuration, loop, temp);
    }

    /**
     * Plays ping-pong sequence (A -> B -> C -> B -> A -> ...).
     * Useful for distinctive looping patterns without starting point ambiguity.
     * E.g. playPingPong(colors, 200, 100) -> 200ms fade + 100ms pause at each color.
     */
    void playPingPong(const std::vector<RGBColor>& colors, unsigned long transitionDuration = 200, unsigned long pauseDuration = 100, bool loop = true, bool temp = false) {
        playSequence(colors, transitionDuration, pauseDuration, loop, temp);
    }

    /**
     * Pre-configured RGB ping-pong (Red -> Green -> Blue -> Green -> Red).
     */
    void playRgbPingPong(unsigned long transitionDuration = 200, unsigned long pauseDuration = 100, bool loop = true, bool temp = false) {
        playPingPong(RGB_PINGPONG_PALETTE, transitionDuration, pauseDuration, loop, temp);
    }

    /**
     * Pre-configured CMY ping-pong (Cyan -> Magenta -> Yellow -> Magenta -> Cyan).
     */
    void playCmyPingPong(unsigned long transitionDuration = 200, unsigned long pauseDuration = 100, bool loop = true, bool temp = false) {
        playPingPong(CMY_PINGPONG_PALETTE, transitionDuration, pauseDuration, loop, temp);
    }

    void update() {
        bool canInterrupt = (currentMode == MODE_IDLE || currentMode == MODE_BLINKING);
        if (seqInterruptMode == SEQ_MODE_IMMEDIATE) {
            canInterrupt = canInterrupt || (currentMode == MODE_SEQUENCE);
        }
        if (count > 0 && canInterrupt) {
            startNextOrder();
        }

        if (currentMode == MODE_FADING) handleFade();
        else if (currentMode == MODE_BLINKING) handlePulse();
        else if (currentMode == MODE_SEQUENCE) handleSequence();
    }

private:
    void startNextOrder() {
        LedOrder next = queue[tail];
        tail = (tail + 1) % MAX_QUEUE;
        count--;

        if (!next.isTemp) lastNormalOrder = next;

        if (next.isReturn) {
            unsigned long returnFadeTime = next.duration;
            next = lastNormalOrder;
            next.entranceDuration = returnFadeTime;
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

    void handleFade() {
        unsigned long dur = activeOrder.entranceDuration;
        unsigned long elapsed = millis() - startTime;
        float progress = (dur == 0) ? 1.0f : (float)elapsed / (float)dur;
        writeInterpolated(startR, startG, startB, activeOrder.r1, activeOrder.g1, activeOrder.b1, progress);
        if (elapsed >= dur) {
            writeRGB(activeOrder.r1, activeOrder.g1, activeOrder.b1);
            currentMode = MODE_IDLE;
        }
    }

    void handlePulse() {
        unsigned long dur = (bPhase == PHASE_ENTRANCE) ? activeOrder.entranceDuration : activeOrder.duration;
        unsigned long elapsed = millis() - startTime;
        float progress = (dur == 0) ? 1.0f : (float)elapsed / (float)dur;

        switch (bPhase) {
        case PHASE_ENTRANCE:
            writeInterpolated(startR, startG, startB, activeOrder.r1, activeOrder.g1, activeOrder.b1, progress);
            if (elapsed >= dur) {
                writeRGB(activeOrder.r1, activeOrder.g1, activeOrder.b1);
                bPhase = PHASE_TO_B;
                startTime = millis();
            }
            break;
        case PHASE_TO_B:
            writeInterpolated(activeOrder.r1, activeOrder.g1, activeOrder.b1, activeOrder.r2, activeOrder.g2, activeOrder.b2, progress);
            if (elapsed >= dur) {
                writeRGB(activeOrder.r2, activeOrder.g2, activeOrder.b2);
                bPhase = PHASE_TO_A;
                startTime = millis();
            }
            break;
        case PHASE_TO_A:
            writeInterpolated(activeOrder.r2, activeOrder.g2, activeOrder.b2, activeOrder.r1, activeOrder.g1, activeOrder.b1, progress);
            if (elapsed >= dur) {
                writeRGB(activeOrder.r1, activeOrder.g1, activeOrder.b1);
                bPhase = PHASE_TO_B;
                startTime = millis();
            }
            break;
        }
    }

    void handleSequence() {
        if (currentSequence.colors.empty()) {
            currentMode = MODE_IDLE;
            return;
        }

        // --- ENTRANCE PHASE: smoothly transition from previous LED color to color 0 ---
        if (currentSequence.inEntrance) {
            unsigned long elapsed = millis() - currentSequence.stepStartTime;
            const RGBColor& target = currentSequence.colors[0];

            if (elapsed >= currentSequence.transitionDuration) {
                writeRGB(target.r, target.g, target.b);
                currentSequence.inEntrance = false;
                currentSequence.currentStep = 0;

                if (currentSequence.pauseDuration > 0) {
                    currentSequence.isPausing = true;
                    currentSequence.pauseStartTime = millis();
                } else {
                    currentSequence.isPausing = false;
                    currentSequence.stepStartTime = millis();
                }
            } else {
                float progress = (currentSequence.transitionDuration == 0) ? 1.0f : (float)elapsed / (float)currentSequence.transitionDuration;
                writeInterpolated(
                    currentSequence.entranceStartR, currentSequence.entranceStartG, currentSequence.entranceStartB,
                    target.r, target.g, target.b,
                    progress
                );
            }
            return;
        }

        // --- PAUSE PHASE: hold on the current color ---
        if (currentSequence.isPausing) {
            unsigned long pauseElapsed = millis() - currentSequence.pauseStartTime;
            if (pauseElapsed >= currentSequence.pauseDuration) {
                if (shouldExitSequence()) {
                    bool wasTemp = currentSequence.temp;
                    unsigned long transDur = currentSequence.transitionDuration;
                    currentSequence.colors.clear();
                    currentMode = MODE_IDLE;
                    if (wasTemp && count == 0) {
                        _push(false, true, true, 0, 0, 0, 0, 0, 0, transDur);
                    }
                    return;
                }
                currentSequence.isPausing = false;
                currentSequence.stepStartTime = millis();
            }
            return;
        }

        // --- TRANSITION PHASE: fade from currentStep to nextStep ---
        size_t numColors = currentSequence.colors.size();
        size_t nextStep = (currentSequence.currentStep + 1) % numColors;
        const RGBColor& cur = currentSequence.colors[currentSequence.currentStep];
        const RGBColor& nxt = currentSequence.colors[nextStep];

        unsigned long elapsed = millis() - currentSequence.stepStartTime;
        if (elapsed >= currentSequence.transitionDuration) {
            writeRGB(nxt.r, nxt.g, nxt.b);
            currentSequence.currentStep = nextStep;
            currentSequence.stepsCompleted++;

            if (currentSequence.pauseDuration == 0) {
                if (shouldExitSequence()) {
                    bool wasTemp = currentSequence.temp;
                    unsigned long transDur = currentSequence.transitionDuration;
                    currentSequence.colors.clear();
                    currentMode = MODE_IDLE;
                    if (wasTemp && count == 0) {
                        _push(false, true, true, 0, 0, 0, 0, 0, 0, transDur);
                    }
                    return;
                }
                currentSequence.stepStartTime = millis();
            } else {
                currentSequence.isPausing = true;
                currentSequence.pauseStartTime = millis();
            }
        } else {
            float progress = (currentSequence.transitionDuration == 0) ? 1.0f : (float)elapsed / (float)currentSequence.transitionDuration;
            writeInterpolated(cur.r, cur.g, cur.b, nxt.r, nxt.g, nxt.b, progress);
        }
    }

    void writeRGB(uint8_t r, uint8_t g, uint8_t b) {
        curR = r; curG = g; curB = b;
        // Auto-reattach if a peripheral (such as tone/buzzer) stole or detached the channel
        if (!ledcWrite(pinR, r)) {
            ledcAttach(pinR, 5000, 8);
            ledcWrite(pinR, r);
        }
        if (!ledcWrite(pinG, g)) {
            ledcAttach(pinG, 5000, 8);
            ledcWrite(pinG, g);
        }
        if (!ledcWrite(pinB, b)) {
            ledcAttach(pinB, 5000, 8);
            ledcWrite(pinB, b);
        }
    }

public:
    void playColor(const String& color) {
        Serial.println(String() + "RGBLed:: playColor:" + color);

        // Permanent Base States
        if      (color == "red")                 fadeTo(255, 0, 0, FADETO_DURATION);
        else if (color == "green")               fadeTo(0, 255, 0, FADETO_DURATION);
        else if (color == "blue")                fadeTo(0, 0, 255, FADETO_DURATION);
        else if (color == "yellow")              fadeTo(255, 255, 0, FADETO_DURATION);
        else if (color == "yellowish")           fadeTo(255, 128, 0, FADETO_DURATION);
        else if (color == "yellowishblue")       fadeTo(255, 128, 64, FADETO_DURATION);
        else if (color == "white")               fadeTo(255, 255, 255, FADETO_DURATION);
        else if (color == "orange")              fadeTo(255, 165, 0, FADETO_DURATION);
        else if (color == "purple")              fadeTo(255, 0, 255, FADETO_DURATION);
        else if (color == "magenta")             fadeTo(255, 0, 255, FADETO_DURATION);
        else if (color == "cyan")                fadeTo(0, 255, 255, FADETO_DURATION);

        // Permanent Pulse States
        else if (color == "bluegreen")           pulse(0, 0, 255, 0, 255, 0, SLOWPULSE_DURATION);

        else if (color == "redpulse")            pulse(0, 0, 0, 255, 0, 0, PULSE_DURATION);
        else if (color == "fastredpulse")        pulse(0, 0, 0, 255, 0, 0, FASTPULSE_DURATION);
        else if (color == "redbluepulse")        pulse(0, 0, 255, 255, 0, 0, FASTPULSE_DURATION);
        else if (color == "bluered")             pulse(0, 0, 255, 255, 0, 0, PULSE_DURATION);

        else if (color == "orangepulse")         pulse(0, 0, 0, 255, 165, 0, PULSE_DURATION);
        else if (color == "fastorangepulse")     pulse(0, 0, 0, 255, 165, 0, FASTPULSE_DURATION);
        else if (color == "ultraorangepulse")    pulse(0, 0, 0, 255, 165, 0, SUPERFASTPULSE_DURATION);

        else if (color == "yellowpulse")         pulse(0, 0, 0, 255, 255, 0, PULSE_DURATION);
        else if (color == "fastyellowpulse")     pulse(0, 0, 0, 255, 255, 0, FASTPULSE_DURATION);
        else if (color == "ultrayellowpulse")    pulse(0, 0, 0, 255, 255, 0, SUPERFASTPULSE_DURATION);

        else if (color == "greenbluepulse")      pulse(0, 255, 255, 0, 255, 0, PULSE_DURATION);

        else if (color == "cyanpulse")           pulse(0, 0, 0, 0, 255, 255, PULSE_DURATION);
        else if (color == "fastcyanpulse")       pulse(0, 0, 0, 0, 255, 255, FASTPULSE_DURATION);
        else if (color == "ultracyanpulse")      pulse(0, 0, 0, 0, 255, 255, SUPERFASTPULSE_DURATION);
        else if (color == "lightningcyanpulse")  pulse(0, 0, 0, 0, 255, 255, LIGHTNINGFADE_DURATION);

        else if (color == "bluishcyanpulse")     pulse(0, 0, 0, 0, 128, 255, PULSE_DURATION);
        else if (color == "bluepulse")           pulse(0, 0, 0, 0, 0, 255, FASTPULSE_DURATION);

        else if (color == "whitepulse")          pulse(0, 0, 0, 255, 255, 255, PULSE_DURATION);
        else if (color == "fastwhitepulse")      pulse(0, 0, 0, 255, 255, 255, FASTPULSE_DURATION);
        else if (color == "ultrawhitepulse")     pulse(0, 0, 0, 255, 255, 255, SUPERFASTPULSE_DURATION);

        else if (color == "purplepulse")         pulse(0, 0, 0, 255, 0, 255, PULSE_DURATION);
        else if (color == "fastpurplepulse")     pulse(0, 0, 0, 255, 0, 255, FASTPULSE_DURATION);
        else if (color == "ultrapurplepulse")    pulse(0, 0, 0, 255, 0, 255, SUPERFASTPULSE_DURATION);

        else if (color == "fastwhitebluepulse")  pulse(255, 255, 255, 0, 255, 0, SUPERFASTPULSE_DURATION);

        // Temporary Interruptions
        else if (color == "tempocyan")           tempFade(0, 255, 255, 300, TEMP_DURATION);
        else if (color == "tempoblue")           tempFade(0, 0, 255, 300, TEMP_DURATION);
        else if (color == "tempored")            tempFade(255, 0, 0, 300, TEMP_DURATION);
        else if (color == "tempoyellow")         tempFade(255, 255, 0, 300, TEMP_DURATION);
        else if (color == "tempoyellowish")      tempFade(255, 128, 0, 300, TEMP_DURATION);
        else if (color == "tempowhite")          tempFade(255, 255, 255, 300, TEMP_DURATION);
        else if (color == "tempoorange")         tempFade(255, 165, 0, 300, TEMP_DURATION);
        else if (color == "tempopurple")         tempFade(255, 0, 255, 300, TEMP_DURATION);

        // Temp then state change
        else if (color == "tempobluethenred")    tempThenFade(0, 0, 255, TEMP_DURATION, TEMPFADEHOLD_DURATION, 255, 0, 0);
        else if (color == "tempoyellowthengreen") tempThenFade(255, 255, 0, TEMP_DURATION, TEMPFADEHOLD_DURATION, 0, 255, 0);
        else if (color == "tempobluethengreen")  tempThenFade(0, 0, 255, TEMP_DURATION, TEMPFADEHOLD_DURATION, 0, 255, 0);
        else if (color == "tempowhitethengreen") tempThenFade(255, 255, 255, TEMP_DURATION, TEMPFADEHOLD_DURATION, 0, 255, 0);
        else if (color == "tempobluethenpulse")  tempThenPulse(0, 0, 255, TEMP_DURATION, TEMPFADEHOLD_DURATION, 255, 0, 0, 255, 255, 255, 500);

        // Sequences (transitionDuration, pauseDuration)
        else if (color == "rainbow")             playRainbow(FADE_DURATION, HOLD_DURATION, true, false); // 400ms fade + 200ms pause at each color
        else if (color == "fastrainbow")         playRainbow(FASTFADE_DURATION, FASTHOLD_DURATION, true, false);   // 160ms fade + 80ms pause
        else if (color == "superfastrainbow")    playRainbow(SUPERFASTFADE_DURATION, SUPERFASTHOLD_DURATION, true, false);   // 160ms fade + 30ms pause
        else if (color == "ultrarainbow")        playRainbow(ULTRAFADE_DURATION, ULTRAHOLD_DURATION, true, false);
        else if (color == "slowrainbow")         playRainbow(SLOWFADE_DURATION, SLOWHOLD_DURATION, true, false); // 800ms fade + 400ms pause
        else if (color == "rainbowtemp")         playRainbow(FASTFADE_DURATION, FASTHOLD_DURATION, true, true);
        else if (color == "magentapulse")        playMagentaRainbow(FASTFADE_DURATION, FASTHOLD_DURATION, true, false);
        else if (color == "fastmagentapulse")    playMagentaRainbow(SUPERFASTFADE_DURATION, SUPERFASTHOLD_DURATION, true, false);
        else if (color == "ultramagentapulse")   playMagentaRainbow(ULTRAFADE_DURATION, ULTRAHOLD_DURATION, true, false);

        // Safe rainbow sequences (without red/orange)
        else if (color == "saferainbow")         playSafeRainbow(FADE_DURATION, HOLD_DURATION, true, false);
        else if (color == "fastsaferainbow")     playSafeRainbow(FASTFADE_DURATION, FASTFADE_DURATION, true, false);
        else if (color == "superfastsaferainbow") playSafeRainbow(SUPERFASTFADE_DURATION, SUPERFASTHOLD_DURATION, true, false);
        else if (color == "ultrasaferainbow")    playSafeRainbow(ULTRAFADE_DURATION, ULTRAHOLD_DURATION, true, false);
        else if (color == "slowsaferainbow")     playSafeRainbow(SLOWFADE_DURATION, SLOWHOLD_DURATION, true, false);
        else if (color == "safemagentapulse")    playSafeMagentaRainbow(FASTFADE_DURATION, 100, true, false);
        else if (color == "fastsafemagentapulse") playSafeMagentaRainbow(SUPERFASTFADE_DURATION, SUPERFASTHOLD_DURATION, true, false);
        else if (color == "ultrasafemagentapulse") playSafeMagentaRainbow(ULTRAFADE_DURATION, ULTRAHOLD_DURATION, true, false);

        // Generic ping-pong sequences (library-provided palettes)
        else if (color == "rgbpingpong")         playRgbPingPong(FADE_DURATION, HOLD_DURATION, true, false);
        else if (color == "fastrgbpingpong")     playRgbPingPong(FASTFADE_DURATION, FASTHOLD_DURATION, true, false);
        else if (color == "ultrargbpingpong")    playRgbPingPong(ULTRAFADE_DURATION, ULTRAHOLD_DURATION, true, false);
        else if (color == "slowrgbpingpong")     playRgbPingPong(SLOWFADE_DURATION, SLOWHOLD_DURATION, true, false);
        else if (color == "cmypingpong")         playCmyPingPong(FADE_DURATION, HOLD_DURATION, true, false);
        else if (color == "fastcmypingpong")     playCmyPingPong(FASTFADE_DURATION, FASTHOLD_DURATION, true, false);
        else if (color == "ultracmypingpong")    playCmyPingPong(ULTRAFADE_DURATION, ULTRAHOLD_DURATION, true, false);
        else if (color == "slowcmypingpong")     playCmyPingPong(SLOWFADE_DURATION, SLOWHOLD_DURATION, true, false);

        // Hardcoded application-specific ping-pong sequences
        else if (color == "yellowbluewhite-pingpong") {
            static const std::vector<RGBColor> palette = {{255, 255, 0}, {0, 0, 255}, {255, 255, 255}, {0, 0, 255}};
            playPingPong(palette, FADE_DURATION, HOLD_DURATION, true, false);
        } else if (color == "fast-yellowbluewhite-pingpong") {
            static const std::vector<RGBColor> palette = {{255, 255, 0}, {0, 0, 255}, {255, 255, 255}, {0, 0, 255}};
            playPingPong(palette, FASTFADE_DURATION, FASTHOLD_DURATION, true, false);
        } else if (color == "slow-yellowbluewhite-pingpong") {
            static const std::vector<RGBColor> palette = {{255, 255, 0}, {0, 0, 255}, {255, 255, 255}, {0, 0, 255}};
            playPingPong(palette, SLOWFADE_DURATION, SLOWHOLD_DURATION, true, false);
        } else if (color == "ultra-yellowbluewhite-pingpong") {
            static const std::vector<RGBColor> palette = {{255, 255, 0}, {0, 0, 255}, {255, 255, 255}, {0, 0, 255}};
            playPingPong(palette, ULTRAFADE_DURATION, ULTRAHOLD_DURATION, true, false);
        } else if (color == "lightning-yellowbluewhite-pingpong") {
            static const std::vector<RGBColor> palette = {{255, 255, 0}, {0, 0, 255}, {255, 255, 255}, {0, 0, 255}};
            playPingPong(palette, LIGHTNINGFADE_DURATION, LIGHTNINGHOLD_DURATION, true, false);
        } else if (color == "cyanpurplemagenta-pingpong") {
            static const std::vector<RGBColor> palette = {{0, 255, 255}, {128, 0, 128}, {255, 0, 255}, {128, 0, 128}};
            playPingPong(palette, FADE_DURATION, HOLD_DURATION, true, false);
        }  else if (color == "fast-cyanpurplemagenta-pingpong") {
            static const std::vector<RGBColor> palette = {{0, 255, 255}, {128, 0, 128}, {255, 0, 255}, {128, 0, 128}};
            playPingPong(palette, FASTFADE_DURATION, FASTHOLD_DURATION, true, false);
        } else if (color == "slow-cyanpurplemagenta-pingpong") {
            static const std::vector<RGBColor> palette = {{0, 255, 255}, {128, 0, 128}, {255, 0, 255}, {128, 0, 128}};
            playPingPong(palette, SLOWFADE_DURATION, SLOWHOLD_DURATION, true, false);
        } else if (color == "ultra-cyanpurplemagenta-pingpong") {
            static const std::vector<RGBColor> palette = {{0, 255, 255}, {128, 0, 128}, {255, 0, 255}, {128, 0, 128}};
            playPingPong(palette, ULTRAFADE_DURATION, ULTRAHOLD_DURATION, true, false);
        } else if (color == "lightning-cyanpurplemagenta-pingpong") {
            static const std::vector<RGBColor> palette = {{0, 255, 255}, {128, 0, 128}, {255, 0, 255}, {128, 0, 128}};
            playPingPong(palette, LIGHTNINGFADE_DURATION, LIGHTNINGHOLD_DURATION, true, false);
        }
    }
};

#endif
