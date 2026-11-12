// collab_util.h - Utilities for Orca collaboration
// Time functions, logging, error handling

#pragma once
#include "../base.h"
#include <stdarg.h>

// Time utilities
// Returns current time in milliseconds since some epoch
U64 collab_time_ms(void);

// Logging
typedef enum {
    COLLAB_LOG_DEBUG = 0,
    COLLAB_LOG_INFO = 1,
    COLLAB_LOG_WARN = 2,
    COLLAB_LOG_ERROR = 3,
} Collab_log_level;

// Set minimum log level (default: COLLAB_LOG_INFO)
void collab_log_set_level(Collab_log_level level);

// Get current log level
Collab_log_level collab_log_get_level(void);

// Log a message
void collab_log(Collab_log_level level, const char *fmt, ...);

// Convenience macros
#define COLLAB_LOG_DEBUG(...) collab_log(COLLAB_LOG_DEBUG, __VA_ARGS__)
#define COLLAB_LOG_INFO(...) collab_log(COLLAB_LOG_INFO, __VA_ARGS__)
#define COLLAB_LOG_WARN(...) collab_log(COLLAB_LOG_WARN, __VA_ARGS__)
#define COLLAB_LOG_ERROR(...) collab_log(COLLAB_LOG_ERROR, __VA_ARGS__)

// Hash utilities
// Compute xxHash64 of data
U64 collab_hash64(const void *data, Usz len);

// Error handling
typedef struct {
    char message[256];
    int code;
} Collab_error;

// Clear error state
void collab_error_clear(Collab_error *err);

// Set error state
void collab_error_set(Collab_error *err, int code, const char *fmt, ...);

// Check if error is set
bool collab_error_is_set(const Collab_error *err);

// Byte order utilities for network serialization (big-endian)
U16 collab_hton16(U16 val);
U32 collab_hton32(U32 val);
U64 collab_hton64(U64 val);
U16 collab_ntoh16(U16 val);
U32 collab_ntoh32(U32 val);
U64 collab_ntoh64(U64 val);

// Write integers to buffer in network byte order
void collab_write_u16(U8 *buf, U16 val);
void collab_write_u32(U8 *buf, U32 val);
void collab_write_u64(U8 *buf, U64 val);

// Read integers from buffer in network byte order
U16 collab_read_u16(const U8 *buf);
U32 collab_read_u32(const U8 *buf);
U64 collab_read_u64(const U8 *buf);

// User-friendly error messages for common network errors
// Returns a static string that doesn't need to be freed
const char *collab_error_user_friendly(int errcode);

// Set error with user-friendly message based on errno
void collab_error_set_network(Collab_error *err, int errcode,
                               const char *operation, const char *host, U16 port);
