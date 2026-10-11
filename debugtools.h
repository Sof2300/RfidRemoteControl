#ifndef DEBUGTOOLS_H
#define DEBUGTOOLS_H

// Master switch for all diagnostics
#ifndef ENABLE_LIFECYCLE_DEBUG
#define ENABLE_LIFECYCLE_DEBUG 1
#endif

// If disabled, all macros compile to nothing
#if !ENABLE_LIFECYCLE_DEBUG
#define DEBUG_LOG(fmt, ...)
#define DEBUG_LOG_LIFECYCLE(fmt, ...)
#define DEBUG_LOG_CB(fmt, ...)
#define DEBUG_LOG_TASK(fmt, ...)
#else
#include <Arduino.h>

// Basic debug logging
#define DEBUG_LOG(fmt, ...) Serial.printf("[DEBUG] " fmt "\n", ##__VA_ARGS__)

// Lifecycle event logging
#define DEBUG_LOG_LIFECYCLE(fmt, ...) Serial.printf("[LIFECYCLE] " fmt "\n", ##__VA_ARGS__)

// TCP callback logging
#define DEBUG_LOG_CB(fmt, ...) Serial.printf("[CB] " fmt "\n", ##__VA_ARGS__)

// Task-related logging
#define DEBUG_LOG_TASK(fmt, ...) Serial.printf("[TASK] " fmt "\n", ##__VA_ARGS__)

#endif // ENABLE_LIFECYCLE_DEBUG

#endif // DEBUGTOOLS_H
