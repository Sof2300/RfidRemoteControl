

Let me structure the review:

1. Overall architecture summary
2. Antipatterns / issues (with code references)
3. Suggested patterns/refactorings
4. Prioritized recommendations


## 1. What the code does (architecture at a glance)

The firmware is a Wi‑Fi RFID access controller organized in roughly four layers:

- **Entry / orchestration** — [RfidRemoteControl.ino](file:///c:/Users/Shpok/Documents/Arduino/RfidRemoteControl/RfidRemoteControl.ino): `setup()`/`loop()`, wiring of LED, buzzer, button, reader, WiFi.
- **State machine** — [RfidStateMachine.h](file:///c:/Users/Shpok/Documents/Arduino/RfidRemoteControl/RfidStateMachine.h) built on the generic engine [StateMachine.h](file:///c:/Users/Shpok/Documents/Arduino/RfidRemoteControl/StateMachine.h). Reads tags/button/WiFi events → drives behavior.
- **Domain/data** — [DataManager.h](file:///c:/Users/Shpok/Documents/Arduino/RfidRemoteControl/DataManager.h) (HTTP calls + config), [RfidDB.h](file:///c:/Users/Shpok/Documents/Arduino/RfidRemoteControl/RfidDB.h) (CSV tag↔id database + undo), [RemoteSyncFile.h](file:///c:/Users/Shpok/Documents/Arduino/RfidRemoteControl/RemoteSyncFile.h) (server sync/patch protocol).
- **Platform helpers** — [WifiManager.h](file:///c:/Users/Shpok/Documents/Arduino/RfidRemoteControl/WifiManager.h), [FileUtils.h](file:///c:/Users/Shpok/Documents/Arduino/RfidRemoteControl/FileUtils.h), [RGBLed.h](file:///c:/Users/Shpok/Documents/Arduino/RfidRemoteControl/RGBLed.h), [ConfigMode.h](file:///c:/Users/Shpok/Documents/Arduino/RfidRemoteControl/ConfigMode.h).

The state machine fluent API is genuinely nice and the sync/undo design is thoughtful. The problems are mostly about **where code lives**, **duplication**, and **global mutable state**.

---

## 2. Antipatterns found

### 2.3 The state machine leaks memory and has unsafe fixed buffers
- [StateMachine.h:L92-L94](file:///c:/Users/Shpok/Documents/Arduino/RfidRemoteControl/StateMachine.h#L92-L94) — `on()` returns `*new Transition(...)`; [onEnter/onLeave and `StateMachine::on`](file:///c:/Users/Shpok/Documents/Arduino/RfidRemoteControl/StateMachine.h#L168-L185) also heap-allocate. Nothing is ever `delete`d, and there is no owner. It happens once at startup so it "works", but it's unowned heap with no lifetime story.
- [StateMachine.h:L190-L204](file:///c:/Users/Shpok/Documents/Arduino/RfidRemoteControl/StateMachine.h#L190-L204) — `doTransition` uses `State* pathSrc[10]; State* pathTarg[10];` with **no bounds check**. A hierarchy deeper than 10 silently overflows the stack.

**Pattern:** own transitions in `std::vector`/containers inside `State`, and replace the fixed arrays with a `std::vector<State*>` or a bounded loop.

 

### 2.5 God classes
- [DataManager.h](file:///c:/Users/Shpok/Documents/Arduino/RfidRemoteControl/DataManager.h) — one class owns config loading, URL encoding, JSON parsing, HTTP transport, error-string classification, status text, and message sending. `RfidStateMachine` has a 500+ line nested `StateSynced` class.
- [RfidDB.h](file:///c:/Users/Shpok/Documents/Arduino/RfidRemoteControl/RfidDB.h) — mixes CSV file I/O, tag parsing (`StructuredTag`), HTTP sync (via `RemoteSyncFile`), and undo bookkeeping.

**Pattern:** split by responsibility — e.g. `HttpApiClient` (the `fetch*` calls), `ConfigStore` (configMap), `TagParser` (`StructuredTag`), `CsvStore` (find/get/append). Composition instead of inheritance where possible.

### 2.6 Copy-pasted async request boilerplate
`fetchSetId`, `fetchSetCommand`, `fetchSaveCommand`, `fetchCurrentLine`, `fetchRestoreLine`, `fetchCommand`, `fetchGenerate`, `fetchSave`, `fetchUndo`, `sendMessage` all repeat the same shape (`requestbusy=true` → success lambda → error lambda → `requestbusy=false` → `dispatchEventSM`):

- [DataManager.h:L204-L530](file:///c:/Users/Shpok/Documents/Arduino/RfidRemoteControl/DataManager.h#L204-L530)

The manual `requestbusy` flag is toggled in ~20 places; one missed reset hangs the whole state machine (the `tagFound` busy-check depends on it). 

**Pattern:** a single private `request(url, onSuccess, onFail)` helper (ideally with RAII/scope-guard for `requestbusy`) and one small typed response-handling function.

### 2.7 Duplicated CSV streaming scanners
- [RfidDB.h:L328-L379](file:///c:/Users/Shpok/Documents/Arduino/RfidRemoteControl/RfidDB.h#L328-L379) `findInCSV` and [L381-L440](file:///c:/Users/Shpok/Documents/Arduino/RfidRemoteControl/RfidDB.h#L381-L440) `getKeyByValue` are two near-identical block readers (1 KB carry-over logic duplicated). `BLOCK_SIZE` is also hardcoded here while `BLOCK_SIZE_DEFINE` exists in [FileUtils.h:L10](file:///c:/Users/Shpok/Documents/Arduino/RfidRemoteControl/FileUtils.h#L10).

**Pattern:** a single `forEachCsvLine(predicate)` iterator; both lookups become one-liners. 

### 2.8 State coupling via cache side-effects
`set()`/`erase()` implicitly depend on `findInCSV` having populated `lastFoundStart/lastFoundEnd` ([RfidDB.h:L110-L170](file:///c:/Users/Shpok/Documents/Arduino/RfidRemoteControl/RfidDB.h#L110-L170)). And the undo stack stores byte offsets that a server file update can invalidate. This "hidden context" makes the API easy to misuse.

**Pattern:** return an explicit result object `{found, value, start, end}` from the lookup instead of caching into members.

### 2.9 Blocking calls inside callbacks / loops
 
- `delayWithRgb` ([.ino:L205-L211](file:///c:/Users/Shpok/Documents/Arduino/RfidRemoteControl/RfidRemoteControl.ino#L205-L211)) is a busy-wait; the main loop is a fixed `delayWithRgb(30,4)`.
 

 

### 2.11 Minor correctness smells
- [RfidStateMachine.h:L419](file:///c:/Users/Shpok/Documents/Arduino/RfidRemoteControl/RfidStateMachine.h#L419) — `"null"` used as a transition target state name; would fail `findState` and silently do nothing if the target is expected. Worth confirming intent.
- Magic numbers: `SYNC_TIMEOUT 5000`, `delay(3000)`, `1024`, `BLOCK_SIZE_DEFINE`, and `SYNC_WAIT_DURATION` scattered rather than centralized.

 Unowned `new Transition/State` | Own them in `std::vector` members |

---

## 3. Patterns that would make it clearer

| Problem area | Suggested pattern |
|---|---|
| One giant translation unit, globals | Header/source split + class-owned state; `extern` only for real singletons |
| String events | `enum class Event : uint8_t` |
| Unowned `new Transition/State` | Own them in `std::vector` members |
| Fixed `pathSrc[10]` arrays | `std::vector<State*>` |
| Guard side effects | Pure guards; effects in `onEnter`/actions |
| Repeated fetch boilerplate | One `request()` helper + RAII `requestbusy` guard |
| Duplicated CSV scanners | Single `forEachCsvLine()` iterator |
| Hidden cache side-effects | Return a `LookupResult` struct |
| God classes | Split by responsibility (HttpClient / Config / CsvStore / TagParser) |
| Blocking delays in callbacks | Non-blocking timers driven from `loop()` |
| Stringly logging/allocs | `Serial.printf` / a `Logger` abstraction |

---

## 4. Suggested priority order

1. **Low-risk, high-value:** extract the async-request helper and the CSV line iterator (kills the worst duplication); delete dead files/blocks.
2. **Correctness:** bounds-check `doTransition`; make `requestbusy` RAII; remove `delay(3000)` from the WiFi callback.
3. **Structural:** introduce `enum class Event`; split `DataManager`; give the state machine ownership of its transitions.
4. **Long-term:** header/source split and a `Logger` so the engine and modules aren't coupled to `Serial`.

Would you like me to start with a specific one? I'd recommend the two duplication refactors (fetch helper + CSV iterator) as the safest first step — I can prepare a diff preview for whichever file you want to begin