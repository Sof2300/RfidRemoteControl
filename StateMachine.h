#ifndef STATEMACHINE_H
#define STATEMACHINE_H

#pragma once
#include <Arduino.h>
#include <string.h>
#include <functional>
#include <memory>
#include <vector>

// Define STATE_MACHINE_DEBUG before including this file to enable detailed diagnostics.
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
		// Transition execution remains visible even when detailed diagnostics are disabled.
		Serial.print(String()+"STATE MACHINE: execute transition for event '"+eventName+"'");
		if (targetName) Serial.println(String()+"' -> '"+targetName+"'");
		else Serial.println("' (no state change)");

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
	std::vector<std::unique_ptr<Transition>> ownedTransitions;

	std::function<void()> _enterFunc;
	std::function<void()> _leaveFunc;

	// SINGLE Unified Constructor!
	State(const char* n, StateMachine* p, std::function<void(State&)> builder = nullptr);

	virtual void enter() {
		Serial.println(String()+"STATE MACHINE: enter state '"+name+"'");
		if (_enterFunc) _enterFunc(); }
	virtual void leave() { if (_leaveFunc) _leaveFunc(); }
	virtual bool isMachine() { return false; }

	Transition& addTransition(const char* eventName, const char* targetName = nullptr) {
		auto transition = std::unique_ptr<Transition>(new Transition(this, eventName, targetName));
		Transition& reference = *transition;
		ownedTransitions.push_back(std::move(transition));
		return reference;
	}

	// --- Fluent API Helpers moved to State ---
	Transition& on(const char* eventName, const char* targetName = nullptr) {
		return addTransition(eventName, targetName);
	}
	// A null target handles the event without leaving or re-entering this state.
	Transition& onSelfInternal(const char* eventName) {
		return on(eventName, nullptr);
	}
	// Targeting this state's name forces an exit and re-entry.
	Transition& onSelfExternal(const char* eventName) {
		return on(eventName, name);
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
#ifdef STATE_MACHINE_DEBUG
		Serial.println(String()+"STATE MACHINE: dispatch event '"+eventName+"'");
		traceActiveConfiguration();
#endif

		State* curr = this;

		while (curr->isMachine() && ((StateMachine*)curr)->activeChild) {
			curr = ((StateMachine*)curr)->activeChild;
		}

#ifdef STATE_MACHINE_DEBUG
		bool isActiveLeaf = true;
#endif
		while (curr) {
#ifdef STATE_MACHINE_DEBUG
			Serial.println(String()+"STATE MACHINE: checking "+(isActiveLeaf ? "active leaf" : "active ancestor")+" '"+curr->name+"'");
			bool foundEventTransition = false;
#endif
			for (Transition* t = curr->transitions; t; t = t->next) {
				if (strcmp(t->eventName, eventName) == 0) {
#ifdef STATE_MACHINE_DEBUG
					foundEventTransition = true;
					Serial.println(String()+"STATE MACHINE: event matches transition declared on '"+curr->name+"'");
#endif
					if (!t->guard(source)) {
#ifdef STATE_MACHINE_DEBUG
						Serial.println("STATE MACHINE: transition guard rejected event");
#endif
						continue;
					}

#ifdef STATE_MACHINE_DEBUG
					Serial.println("STATE MACHINE: transition guard accepted event");
#endif
					t->action(source);
					if (t->targetName) {
						State* target = findState(t->targetName);
						if (target) {
#ifdef STATE_MACHINE_DEBUG
							Serial.println(String()+"STATE MACHINE: applying transition from '"+curr->name+"' to '"+target->name+"'");
#endif
							doTransition(curr, target);
						} else {
#ifdef STATE_MACHINE_DEBUG
							Serial.println(String()+"STATE MACHINE ERROR: transition target not found: '"+t->targetName+"'");
#endif
						}
					} else {
#ifdef STATE_MACHINE_DEBUG
						Serial.println(String()+"STATE MACHINE: internal transition on '"+curr->name+"' (no state change)");
#endif
					}
					return;
				}
			}
#ifdef STATE_MACHINE_DEBUG
			if (!foundEventTransition) {
				Serial.println(String()+"STATE MACHINE: no matching transition on '"+curr->name+"'");
			}
#endif
			curr = (State*)curr->parent;
#ifdef STATE_MACHINE_DEBUG
			isActiveLeaf = false;
#endif
		}
#ifdef STATE_MACHINE_DEBUG
		Serial.println(String()+"STATE MACHINE: event '"+eventName+"' was not handled");
#endif
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
#ifdef STATE_MACHINE_DEBUG
			Serial.print("STATE MACHINE ERROR: Could not find source state: ");
			Serial.println(sourceName);
#endif
			sourceState = this; // Safe fallback to prevent crash
		}

		return sourceState->addTransition(eventName, targetName);
	}

    void onEnter(const char* stateName, std::function<void()> action) {
        State* targetState = findState(stateName);
        if (targetState) {
            targetState->_enterFunc = action;
        } else {
#ifdef STATE_MACHINE_DEBUG
            Serial.print("STATE MACHINE ERROR: onEnter could not find state: ");
            Serial.println(stateName);
#endif
        }
    }

    void onLeave(const char* stateName, std::function<void()> action) {
        State* targetState = findState(stateName);
        if (targetState) {
            targetState->_leaveFunc = action;
        } else {
#ifdef STATE_MACHINE_DEBUG
            Serial.print("STATE MACHINE ERROR: onLeave could not find state: ");
            Serial.println(stateName);
#endif
        }
    }

private:
#ifdef STATE_MACHINE_DEBUG
	void traceActiveConfiguration() {
		Serial.print("STATE MACHINE: active configuration: ");
		State* curr = this;
		bool first = true;
		while (curr) {
			if (!first) Serial.print(" -> ");
			Serial.print(curr->name);
			first = false;
			if (!curr->isMachine()) break;

			StateMachine* machine = (StateMachine*)curr;
			if (!machine->activeChild) {
				Serial.print(" [no active child]");
				break;
			}
			curr = machine->activeChild;
		}
		Serial.println();
	}
#endif

	void doTransition(State* sourceLeaf, State* target) {
		std::vector<State*> pathSrc;
		std::vector<State*> pathTarg;

		for (State* s = sourceLeaf; s; s = (State*)s->parent) pathSrc.push_back(s);
		for (State* s = target; s; s = (State*)s->parent) pathTarg.push_back(s);

		State* lca = nullptr;
		int i = static_cast<int>(pathSrc.size()) - 1;
		int j = static_cast<int>(pathTarg.size()) - 1;
		if (sourceLeaf == target) {
			lca = (State*)sourceLeaf->parent; i--; j--;
		} else {
			while (i >= 0 && j >= 0 && pathSrc[static_cast<size_t>(i)] == pathTarg[static_cast<size_t>(j)]) {
				lca = pathSrc[static_cast<size_t>(i)];
				i--;
				j--;
			}
		}

		for (int k = 0; k <= i; k++) {
			State* current = pathSrc[static_cast<size_t>(k)];
			current->leave();
			if (current->parent) current->parent->activeChild = nullptr;
		}

		for (int k = j; k >= 0; k--) {
			State* current = pathTarg[static_cast<size_t>(k)];
			if (current->parent) current->parent->activeChild = current;
			current->enter();
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
