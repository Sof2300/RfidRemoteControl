#ifndef STATEMACHINE_EXAMPLE_H
#define STATEMACHINE_EXAMPLE_H

#include "StateMachine.h"

// ==========================================
// 1. EMITTERS (Event Sources)
// ==========================================
struct BadgeReader : EventSource {
    static const int TYPE_ID = 1;
    const char* lastTag;
    
    BadgeReader() : EventSource(TYPE_ID) {} 
    
    bool isVIP() { return strcmp(lastTag, "VIP-123") == 0; }
    void simulateSwipe(StateMachine& fsm, const char* tag) {
        lastTag = tag;
        Serial.printf("\n[BadgeReader] Swiped tag: %s\n", tag);
        fsm.dispatch("swipe", this);
    }
};

struct WaterTank : EventSource {
    static const int TYPE_ID = 2;
    int ml_remaining;
    
    WaterTank(int ml) : EventSource(TYPE_ID), ml_remaining(ml) {} 
    
    bool isEmpty() { return ml_remaining <= 0; }
    void drain(int amount) { ml_remaining -= amount; }
};

// Global Hardware
BadgeReader badgeReader;
WaterTank   waterTank{500};

// ==========================================
// STYLE 1: FLUENT API (Explicit Context)
// ==========================================
struct CoffeeMakerFluent : StateMachine {

    // Root machine (Parent is nullptr, starting state is "off")
    CoffeeMakerFluent() : StateMachine("CoffeeMaker", nullptr, "off") {}

    // Child of Root (Pass 'this' as parent)
    State off{"off", this, [](State& s) {
        s.onEnter([]() { Serial.println("-> [Fluent] MACHINE IS OFF"); });
        s.on("switch on", "on_mode");
    }};

    // --- HIERARCHY START ---
    // Nested StateMachine (Child of Root, pass 'this' as parent, starts at "idle")
    StateMachine on_mode{"on_mode", this, "idle", [](StateMachine& sm) {
        // MAGIC OF HSM: Attach "switch off" to the parent state!
        // Whether we are in 'idle' or 'brewing', this transition will catch it
        // and exit the nested machine cleanly back to 'off'.
        sm.on("switch off", "off");
    }};

    // Child of nested machine (Pass '&on_mode' as parent)
    State idle{"idle", &on_mode, [](State& s) {
        s.onEnter([]() { Serial.println("-> [Fluent] IDLE: Waiting for VIP badge..."); });
        
        // 1. Transition to brewing
        s.on("swipe", "brewing")
         .attachGuard([](EventSource& e) {
             auto r = e.get<BadgeReader>();
             return r && r->isVIP() && !waterTank.isEmpty();
         });

        // 2. Reject non-VIP (internal transition, stays in idle)
        s.on("swipe")
         .attachGuard([](EventSource& e) {
             auto r = e.get<BadgeReader>();
             return r && !r->isVIP();
         })
         .attachAction([](EventSource&) {
             Serial.println("  [Fluent] Access Denied. Not a VIP.");
         });

        // 3. Reject VIP if tank is empty
        s.on("swipe")
         .attachGuard([](EventSource& e) {
             auto r = e.get<BadgeReader>();
             return r && r->isVIP() && waterTank.isEmpty();
         })
         .attachAction([](EventSource&) {
             Serial.println("  [Fluent] Error: Tank Empty!");
         });
    }};

    // Child of nested machine (Pass '&on_mode' as parent)
    State brewing{"brewing", &on_mode, [](State& s) {
        s.onEnter([]() {
            Serial.println("-> [Fluent] BREWING: Pouring coffee...");
            waterTank.drain(250);
        });
        s.on("done", "idle");
    }};
    // --- HIERARCHY END ---
};


// ==========================================
// STYLE 2: OOP API (Zero Dynamic Allocation)
// ==========================================
class CoffeeMakerOOP : public StateMachine {
public:
    // Root Machine
    CoffeeMakerOOP() : StateMachine("CoffeeMaker", nullptr, "off") {}

    class Off : public State {
    public:
        Off(StateMachine* p) : State("off", p) {}
        void enter() override { Serial.println("-> [OOP] MACHINE IS OFF"); }
        Transition switchOn{this, "switch on", "on_mode"};
    } off{this};

    // --- HIERARCHY START ---
    class OnMode : public StateMachine {
    public:
        // Nested Machine: Passes parent 'p', starts at 'idle'
        OnMode(StateMachine* p) : StateMachine("on_mode", p, "idle") {}
        
        // MAGIC OF HSM: Pre-empts all child states (Idle/Brewing)
        Transition switchOff{this, "switch off", "off"};

        class Idle : public State {
        public:
            Idle(StateMachine* p) : State("idle", p) {}
            void enter() override { Serial.println("-> [OOP] IDLE: Waiting for VIP badge..."); }

            struct BrewCmd : Transition {
                using Transition::Transition;
                bool guard(EventSource* source) override {
                    if (!source) return false;
                    if (auto reader = source->get<BadgeReader>()) {
                        return reader->isVIP() && !waterTank.isEmpty();
                    }
                    return false;
                }
            } brew{this, "swipe", "brewing"};

            struct RejectCmd : Transition {
                using Transition::Transition;
                bool guard(EventSource* source) override {
                    if (auto reader = source->get<BadgeReader>()) return !reader->isVIP();
                    return false;
                }
                void action(EventSource* source) override {
                    Serial.println("  [OOP] Access Denied. Not a VIP.");
                }
            } reject{this, "swipe"};

            struct EmptyTankCmd : Transition {
                using Transition::Transition;
                bool guard(EventSource* source) override {
                    if (auto reader = source->get<BadgeReader>()) return reader->isVIP() && waterTank.isEmpty();
                    return false;
                }
                void action(EventSource* source) override {
                    Serial.println("  [OOP] Error: Tank Empty!");
                }
            } emptyTank{this, "swipe"};

        } idle{this}; // 'this' refers to the OnMode machine!

        class Brewing : public State {
        public:
            Brewing(StateMachine* p) : State("brewing", p) {}
            void enter() override {
                Serial.println("-> [OOP] BREWING: Pouring coffee...");
                waterTank.drain(250);
            }
            Transition done{this, "done", "idle"};
        } brewing{this}; // 'this' refers to the OnMode machine!

    } on_mode{this}; // 'this' refers to the Root machine!
    // --- HIERARCHY END ---
};


// ==========================================
// Execution Logic
// ==========================================
CoffeeMakerFluent fsm; 
// CoffeeMakerOOP fsm; // Swap this to test Style 2!

void setup() {
    Serial.begin(115200);
    fsm.start(); 
}

void loop() {
    delay(2000);
    static int step = 0;

    // Sequence demonstrating deep HSM transitions
    if (step == 0) fsm.dispatch("switch on");
    if (step == 1) badgeReader.simulateSwipe(fsm, "GUEST-999");
    if (step == 2) badgeReader.simulateSwipe(fsm, "VIP-123");  

    // Demonstrate we can switch off mid-brew thanks to the Parent Transition!
    if (step == 3) {
        Serial.println("\n[Action] Oh no, canceling brew!");
        fsm.dispatch("switch off");
    }

    if (step == 4) fsm.dispatch("switch on");
    if (step == 5) badgeReader.simulateSwipe(fsm, "VIP-123");
    if (step == 6) fsm.dispatch("done"); // Back to idle normally
    if (step == 7) badgeReader.simulateSwipe(fsm, "VIP-123"); // Fails, empty tank
    if (step == 8) fsm.dispatch("switch off");

    step++;
    if (step > 8) {
        step = 0;
        waterTank.ml_remaining = 500; // Reset for next loop
        Serial.println("\n--- LOOP RESTART ---\n");
    }
}

#endif
