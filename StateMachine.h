#ifndef STATEMACHINE_H
#define STATEMACHINE_H

#pragma once
#include <Arduino.h>
#include <string.h>
#include <functional>

class StateMachine;
class State;

// ==========================================
// Event Sources
// ==========================================
struct EventSource {
	const int typeId;
	EventSource(int id = 0) : typeId(id) {}
	virtual ~EventSource() = default;

	template <typename T>
	T* get() {
		if (this->typeId == T::TYPE_ID) return static_cast<T*>(this);
		return nullptr;
	}
};

static EventSource EmptyEvent(0);

// ==========================================
// HSM Engine
// ==========================================
class Transition {
public:
	const char* eventName;
	const char* targetName;
	Transition* next;

	std::function<bool(EventSource&)> _guardFunc;
	std::function<void(EventSource&)> _actionFunc;

	Transition(State* sourceState, const char* evName, const char* target = nullptr);

	virtual bool guard(EventSource* source) {
		if (_guardFunc) return _guardFunc(source ? *source : EmptyEvent);
		return true;
	}
	virtual void action(EventSource* source) {
		Serial.println(String()+"STATE MACHINE: Transition to state '"+targetName+"' with event '"+eventName+"'");

		if (_actionFunc) _actionFunc(source ? *source : EmptyEvent);
	}

	Transition& attachGuard(std::function<bool(EventSource&)> g) { _guardFunc = g; return *this; }
	Transition& attachAction(std::function<void(EventSource&)> a) { _actionFunc = a; return *this; }
};

class State {
public:
	const char* name;
	StateMachine* parent;
	Transition* transitions;
	State* nextSibling;

	std::function<void()> _enterFunc;
	std::function<void()> _leaveFunc;

	// SINGLE Unified Constructor!
	State(const char* n, StateMachine* p, std::function<void(State&)> builder = nullptr);

	virtual void enter() {
		Serial.println(String()+"STATE MACHINE: enter state '"+name+"'");
		if (_enterFunc) _enterFunc(); }
	virtual void leave() { if (_leaveFunc) _leaveFunc(); }
	virtual bool isMachine() { return false; }

	// --- Fluent API Helpers moved to State ---
	Transition& on(const char* eventName, const char* targetName = nullptr) {
		return *new Transition(this, eventName, targetName);
	}
	void onEnter(std::function<void()> action) { _enterFunc = action; }
	void onLeave(std::function<void()> action) { _leaveFunc = action; }
};

class StateMachine : public State {
public:
	State* children;
	State* activeChild;
	const char* initChildName;

	StateMachine(const char* n, StateMachine* p = nullptr, const char* initChild = nullptr, std::function<void(StateMachine&)> builder = nullptr)
	: State(n, p), children(nullptr), activeChild(nullptr), initChildName(initChild) {
		if (builder) builder(*this);
	}

	void enter() override {
		State::enter();
		if (initChildName) {
			State* init = findState(initChildName);
			if (init) {
				activeChild = init;
				init->enter();
			}
		}
	}

	bool isMachine() override { return true; }

	void start() {
		if (initChildName) {
			State* init = findState(initChildName);
			if (init) doTransition(this, init);
		}
	}

	void dispatch(const char* eventName, EventSource* source = nullptr) {
		State* curr = this;

		while (curr->isMachine() && ((StateMachine*)curr)->activeChild) {
			curr = ((StateMachine*)curr)->activeChild;
		}

		while (curr) {
			for (Transition* t = curr->transitions; t; t = t->next) {
				if (strcmp(t->eventName, eventName) == 0 && t->guard(source)) {
					t->action(source);
					if (t->targetName) {
						State* target = findState(t->targetName);
						if (target) doTransition(curr, target);
					}
					return;
				}
			}
			curr = (State*)curr->parent;
		}
	}
	void dispatch(String eventName) { dispatch(eventName.c_str()); }

	State* findState(const char* targetName) {
		if (strcmp(name, targetName) == 0) return this;
		for (State* s = children; s; s = s->nextSibling) {
			if (s->isMachine()) {
				State* found = ((StateMachine*)s)->findState(targetName);
				if (found) return found;
			} else {
				if (strcmp(s->name, targetName) == 0) return s;
			}
		}
		return nullptr;
	}

	Transition& on(const char* sourceName, const char* eventName, const char* targetName = nullptr) {
		State* sourceState = findState(sourceName);

		if (!sourceState) {
			Serial.print("STATE MACHINE ERROR: Could not find source state: ");
			Serial.println(sourceName);
			sourceState = this; // Safe fallback to prevent crash
		}

		return *new Transition(sourceState, eventName, targetName);
	}

    void onEnter(const char* stateName, std::function<void()> action) {
        State* targetState = findState(stateName);
        if (targetState) {
            targetState->_enterFunc = action;
        } else {
            Serial.print("STATE MACHINE ERROR: onEnter could not find state: ");
            Serial.println(stateName);
        }
    }

    void onLeave(const char* stateName, std::function<void()> action) {
        State* targetState = findState(stateName);
        if (targetState) {
            targetState->_leaveFunc = action;
        } else {
            Serial.print("STATE MACHINE ERROR: onLeave could not find state: ");
            Serial.println(stateName);
        }
    }

private:
	void doTransition(State* sourceLeaf, State* target) {
		State* pathSrc[10]; int depthSrc = 0;
		State* pathTarg[10]; int depthTarg = 0;

		for (State* s = sourceLeaf; s; s = (State*)s->parent) pathSrc[depthSrc++] = s;
		for (State* s = target; s; s = (State*)s->parent) pathTarg[depthTarg++] = s;

		State* lca = nullptr;
		int i = depthSrc - 1, j = depthTarg - 1;
		if (sourceLeaf == target) {
			lca = (State*)sourceLeaf->parent; i--; j--;
		} else {
			while (i >= 0 && j >= 0 && pathSrc[i] == pathTarg[j]) { lca = pathSrc[i]; i--; j--; }
		}

		for (int k = 0; k <= i; k++) {
			pathSrc[k]->leave();
			if (pathSrc[k]->parent) pathSrc[k]->parent->activeChild = nullptr;
		}

		for (int k = j; k >= 0; k--) {
			if (pathTarg[k]->parent) pathTarg[k]->parent->activeChild = pathTarg[k];
			pathTarg[k]->enter();
		}
	}
};

// --- Out of line implementations ---
inline State::State(const char* n, StateMachine* p, std::function<void(State&)> builder)
: name(n), parent(p), transitions(nullptr), nextSibling(nullptr) {
	if (parent) {
		nextSibling = parent->children;
		parent->children = this;
	}
	if (builder) builder(*this);
}

inline Transition::Transition(State* source, const char* evName, const char* target)
: eventName(evName), targetName(target), next(nullptr) {
	if (source) {
		next = source->transitions;
		source->transitions = this;
	}
}
#endif
