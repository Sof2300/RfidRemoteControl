# RfidRemoteControl

RfidRemoteControl is an Arduino/ESP32-based project that manages RFID-based remote control and access systems. It provides utilities for reading RFID tags, mapping tag IDs to user data, serving or syncing configuration and data over Wi-Fi, and controlling connected peripherals such as RGB LEDs.

## Purpose

- Provide a compact, extensible firmware for ESP32-based RFID readers.
- Maintain a local database mapping RFID tag IDs to identifiers or actions.
- Offer synchronization and remote management of configuration and data via HTTP/Wi‑Fi.
- Demonstrate a state-machine driven architecture suitable for access control and automation prototypes.

## Features

- Read RFID tags and lookup mapped IDs from CSV/JSON data files.
- Local data storage and simple file utilities (e.g., `data/` folder for `config.json`, `RFID2ID.csv`).
- Wi-Fi connection management and optional HTTP-based syncing/remote endpoints.
- State machine implementation to drive system behavior (`StateMachine.h`, `RfidStateMachine.h`).
- RGB LED control utilities for visual feedback (`RGBLed.h`).
- Example and legacy helpers for migration (`RfidDB-v0.h`, `RemoteSyncFile-v0.h`).

## Repository organization

- `*.ino`
  - `RfidRemoteControl.ino` — main Arduino sketch / entry point.
- Core headers
  - `DataManager.h`, `DataManager2.h` — data access and management layers.
  - `RfidDB.h`, `RfidDB-v0.h` — RFID database helpers and legacy versions.
  - `StateMachine.h`, `RfidStateMachine.h`, `StateMachine-example.h` — state machine framework and examples.
  - `RGBLed.h` — RGB LED driver and helpers.
  - `WifiManager.h` — Wi-Fi setup and connection management.
  - `RemoteSyncFile.h`, `RemoteSyncFile-v0.h`, `RemoteSyncServer.h` — remote sync utilities.
  - `AsyncHttpRequest.h`, `HttpReader.h` — HTTP helpers.
  - `FileUtils.h`, `SimpleFSEsp32.h` — filesystem utilities for SPIFFS/LittleFS.

- Config & data
  - `data/config.json` — runtime configuration file.
  - `data/RFID2ID.csv` — simple mapping from RFID tag to application ID.

## Getting started

1. Install Arduino IDE or PlatformIO with ESP32 board support.
2. Open `RfidRemoteControl.ino` and configure Wi-Fi and hardware pins in `Definitions.h` or `config.json`.
3. Upload to an ESP32-based board connected to an RFID reader (e.g., RC522) and RGB LED.
4. Use the `data/RFID2ID.csv` or JSON alternatives to map tags to IDs, or use the web UI to manage entries.


## Notes

- Many helper files include legacy or experimental code (e.g., `-v0` and `-depr` suffixes). Review before reuse.
- This repository mixes Arduino sketch files with web assets under `otherdata/`; keep static UI assets updated when changing data formats.

Superscale operations:

- Four primary operations are supported: 
 -`set id` : assign current ID on Superscale, 
 - `set command` : assign current command on Superscale, 
 - `attach command` : bind Superscale Superscale current command to an RFID, and 
 - `erase rfid` :remove a tag/id mapping.
- Superscale HTTP endpoints used :
  - `set?id=...` — set/confirm an ID for the scanned tag
  - `set?command=...` — trigger a command on Superscale
  - `json` — read latest command payload (used when attaching commands)
  - `generateID?nohtml=1` and `save` — generate and save new IDs

 
 
Remote sync and multi-device coordination:

- Devices synchronize the master `RFID2ID.csv` via Superscale endpoints: `sync_check`, `sync_download`, `sync_download_delta`, `sync_push_delta`, and `sync_patch`.
- Typical flow: device calls `sync_check` (sends size+CRC); the server replies `UP_TO_DATE`, `FULL_UPDATE`, `PULL_DELTA:...`, or `PUSH_DELTA` so the device downloads deltas/full file or pushes its local changes.
- Conflicts are detected with CRC/If-Match checks and duplicate-ID errors; the protocol surfaces conflicts so server version prevails.
- This protocol lets multiple ESP32 devices safely share and update the same RFID mapping file on Superscale.
 

