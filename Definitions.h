#ifndef DEFINITION_H
#define DEFINITION_H

#include <Arduino.h>


#define APP_NAME "Tzizika"// Rfid Remote Control"


//////////////////////////////////////////

#define SUPERSCALE_URL_FIELD "superscaleurl"
#define COMMAND_KEYWORD "command"
#define DISABLESAVE_KEYWORD "disable-rfid-id-save"
#define TRANSIENTTAG_KEYWORD "transient-tag"
#define COMMANDDOT_KEYWORD "command:"
#define ULTCOMMAND_KEYWORD "meta:command"
#define ERASE_COMMAND_KEYWORD "meta:erase"
#define UNDO_COMMAND_KEYWORD "meta:undo"
#define RFID_UNDO_COMMAND_KEYWORD "meta:rfid-undo"
#define SSID_KEY "ssid"
#define PASSWORD_KEY "password"
#define WIFI_RECONNECT_SEC "wifireconnectsec"

#define COMM_CHANNEL_NAME "comm"
#define SYNC_CHANNEL_NAME "sync"
#define MESSAGE_CHANNEL_NAME "message"

#define EVENT_NOSUPERSCALE_CONFIG "NOSUPERSCALE_CONFIG"
#define EVENT_WIFI_CONNECTING "WIFI_CONNECTING"
#define EVENT_WIFI_ERROR "WIFI_ERROR"
#define EVENT_WIFI_CONNECTED "WIFI_CONNECTED"
#define EVENT_WIFI_LOST "WIFI_LOST"

#define EVENT_SYNC_INPROGRESS "SYNC_INPROGRESS"
#define EVENT_SYNC_SUCCESS "SYNC_SUCCESS"
#define EVENT_SYNC_ERROR "SYNC_ERROR"
#define EVENT_SYNC_NOSUPERSCALE "SYNC_NOSUPERSCALE"
#define EVENT_SYNC_WAIT "EVENT_SYNC_WAIT"

#define EVENT_RFID_BUSY "RFID_BUSY"
#define EVENT_BUTTON_PRESS "BUTTON_PRESS"
#define EVENT_RFID_KNOWN "RFID_KNOWN"
#define EVENT_RFID_UNKNOWN "RFID_UNKNOWN"
#define EVENT_RFID_COMMAND "RFID_COMMAND"
#define EVENT_RFID_ATTACHCOMMAND "RFID_ATTACHCOMMAND"
#define EVENT_RFID_ERASE "RFID_ERASE"
#define EVENT_RFID_UNDO "RFID_UNDO"
#define EVENT_RFID_RFIDUNDO "RFID_RFIDUNDO"

#define EVENT_SET_SUCCESS "SET_SUCCESS"
#define EVENT_SET_FAIL "SET_FAIL"
#define EVENT_GENERATE_SUCCESS "GENERATE_SUCCESS"
#define EVENT_GENERATE_FAIL "GENERATE_FAIL"
#define EVENT_GENERATEID_INVALID "GENERATEID_INVALID"
#define EVENT_SAVE_SUCCESS "SAVE_SUCCESS"
#define EVENT_SAVE_FAIL "SAVE_FAIL"
#define EVENT_SET_COMMAND_SUCCESS "SET_COMMAND_SUCCESS"
#define EVENT_SET_COMMAND_FAIL "SET_COMMAND_FAIL"
#define EVENT_SAVE_COMMAND_SUCCESS "SAVE_COMMAND_SUCCESS"
#define EVENT_SAVE_COMMAND_FAIL "SAVE_COMMAND_FAIL"
#define EVENT_GET_LINE_SUCCESS "GET_LINE_SUCCESS"
#define EVENT_GET_LINE_FAIL "GET_LINE_FAIL"
#define EVENT_RESTORE_LINE_SUCCESS "RESTORE_LINE_SUCCESS"
#define EVENT_RESTORE_LINE_FAIL "RESTORE_LINE_FAIL"
#define EVENT_GET_COMMAND_SUCCESS "GET_COMMAND_SUCCESS"
#define EVENT_GET_COMMAND_FAIL "GET_COMMAND_FAIL"
#define EVENT_UNDO_SUCCESS "UNDO_SUCCESS"
#define EVENT_UNDO_FAIL "UNDO_FAIL"
#define EVENT_RFIDUNDO_SUCCESS "RFIDUNDO_SUCCESS"
#define EVENT_RFIDUNDO_FAIL "RFIDUNDO_FAIL"

#define EVENT_PATCH_INPROGRESS "PATCH_INPROGRESS"
#define EVENT_PATCH_ERROR "PATCH_ERROR"
#define EVENT_PATCH_SUCCESS "PATCH_SUCCESS"

// color chart: white is virgin, green is ready, yellow is known rfid, white is new ones, blue is saving
#define LED_INIT_COLOR "white"
#define LED_WIFI_CONNECTING_COLOR "yellowpulse"
#define LED_WIFI_FAILED_COLOR "redpulse"
#define LED_WIFI_CONNECTED_COLOR "yellow"
#define LED_NO_SUPERSCALE_COLOR "fastredpulse"
#define LED_ERROR_COLOR "tempored"
#define LED_SYNC_ERROR_COLOR "tempored"
#define LED_SYNCED_COLOR "green"
#define LED_CONFIRM_COLOR "tempoblue"

#define LED_SETINPROGRESS_COLOR "ultrayellowpulse"
#define LED_SETREADY_COLOR "fastyellowpulse"
#define LED_GENERATEINPROGRESS_COLOR "ultrawhitepulse"
#define LED_GENERATEREADY_COLOR "whitepulse"
#define LED_GENERATESAVE_COLOR "fastwhitebluepulse"
#define LED_SYNCING_COLOR "yellowpulse"
#define LED_SYNCINPROGRESS_COLOR "ultrapurplepulse"
#define LED_CONFIRMSYNC_COLOR "tempopurple"
#define LED_SAVEINPROGRESS_COLOR "bluepulse"
#define LED_CONFIRMSAVE_COLOR "tempoblue"

#define LED_COMMANDINPROGRESS_COLOR "ultraorangepulse"
#define LED_COMMAND_COLOR "tempoorange"
#define LED_ATTACHCOMMAND_BLINKCOLOR "ultrapurplepulse"
#define LED_ATTACHCOMMAND_WAITTAG_COLOR "purplepulse"
#define LED_ATTACHCOMMAND_CONFIRM_COLOR "fastpurplepulse"
#define LED_ERASE_BLINKCOLOR "fastcyanpulse"
#define LED_ERASE_BLINKFASTCOLOR "ultracyanpulse"
#define LED_ERASE_CONFIRM_COLOR "lightningcyanpulse"

#define LED_UNDO_BLINKCOLOR "fast-yellowbluewhite-pingpong"
#define LED_UNDO_BLINKFASTCOLOR "ultra-yellowbluewhite-pingpong"
#define LED_UNDO_CONFIRM_COLOR "lightning-yellowbluewhite-pingpong"

#define LED_RFIDUNDO_BLINKCOLOR "fast-cyanpurplemagenta-pingpong"
#define LED_RFIDUNDO_BLINKFASTCOLOR "ultra-cyanpurplemagenta-pingpong"
#define LED_RFIDUNDO_COLOR "lightning-cyanpurplemagenta-pingpong"

#define LED_CONFIGMODE_COLOR "superfastsaferainbow"

#define BUZZER_BOOT "on"


void dispatchEventSM(String eventname);



#endif
