// collab_util.c - Utilities for Orca collaboration
// Time functions, logging, error handling

// Enable POSIX features for clock_gettime
#if !defined(_POSIX_C_SOURCE) || _POSIX_C_SOURCE < 199309L
#undef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 199309L
#endif

#include "collab_util.h"
#include <stdio.h>
#include <time.h>
#include <string.h>
#include <errno.h>
#include <netdb.h>

// Include xxHash implementation
#define XXH_INLINE_ALL
#define XXH_NO_STREAM
#include "../thirdparty/xxhash.h"

// Platform-specific time includes
#if defined(__APPLE__)
#include <mach/mach_time.h>
#else
#include <sys/time.h>
#endif

// Global log level
static Collab_log_level g_log_level = COLLAB_LOG_INFO;

// Time utilities
U64 collab_time_ms(void) {
#if defined(__APPLE__)
    static mach_timebase_info_data_t timebase = {0};
    if (timebase.denom == 0) {
        mach_timebase_info(&timebase);
    }
    U64 now = mach_absolute_time();
    return (now * timebase.numer / timebase.denom) / 1000000;
#elif defined(CLOCK_MONOTONIC)
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (U64)ts.tv_sec * 1000 + (U64)ts.tv_nsec / 1000000;
#else
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (U64)tv.tv_sec * 1000 + (U64)tv.tv_usec / 1000;
#endif
}

// Logging
void collab_log_set_level(Collab_log_level level) {
    g_log_level = level;
}

Collab_log_level collab_log_get_level(void) {
    return g_log_level;
}

static const char *log_level_str(Collab_log_level level) {
    switch (level) {
        case COLLAB_LOG_DEBUG: return "DEBUG";
        case COLLAB_LOG_INFO:  return "INFO";
        case COLLAB_LOG_WARN:  return "WARN";
        case COLLAB_LOG_ERROR: return "ERROR";
        default: return "?";
    }
}

void collab_log(Collab_log_level level, const char *fmt, ...) {
    if (level < g_log_level) {
        return;
    }

    // Get timestamp
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    char time_buf[32];
    strftime(time_buf, sizeof(time_buf), "%H:%M:%S", tm_info);

    // Print log message
    fprintf(stderr, "[%s][COLLAB][%s] ", time_buf, log_level_str(level));

    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);

    fprintf(stderr, "\n");
}

// Hash utilities
U64 collab_hash64(const void *data, Usz len) {
    return XXH64(data, len, 0);
}

// Error handling
void collab_error_clear(Collab_error *err) {
    err->message[0] = '\0';
    err->code = 0;
}

void collab_error_set(Collab_error *err, int code, const char *fmt, ...) {
    err->code = code;

    va_list args;
    va_start(args, fmt);
    vsnprintf(err->message, sizeof(err->message), fmt, args);
    va_end(args);
}

bool collab_error_is_set(const Collab_error *err) {
    return err->code != 0;
}

// Byte order utilities
// Check endianness at runtime
static inline bool is_little_endian(void) {
    U16 x = 1;
    return *((U8 *)&x) == 1;
}

U16 collab_hton16(U16 val) {
    if (is_little_endian()) {
        return (U16)((val >> 8) | (val << 8));
    }
    return val;
}

U32 collab_hton32(U32 val) {
    if (is_little_endian()) {
        return ((val >> 24) & 0x000000FF) |
               ((val >> 8)  & 0x0000FF00) |
               ((val << 8)  & 0x00FF0000) |
               ((val << 24) & 0xFF000000);
    }
    return val;
}

U64 collab_hton64(U64 val) {
    if (is_little_endian()) {
        return ((val >> 56) & 0x00000000000000FFULL) |
               ((val >> 40) & 0x000000000000FF00ULL) |
               ((val >> 24) & 0x0000000000FF0000ULL) |
               ((val >> 8)  & 0x00000000FF000000ULL) |
               ((val << 8)  & 0x000000FF00000000ULL) |
               ((val << 24) & 0x0000FF0000000000ULL) |
               ((val << 40) & 0x00FF000000000000ULL) |
               ((val << 56) & 0xFF00000000000000ULL);
    }
    return val;
}

U16 collab_ntoh16(U16 val) {
    return collab_hton16(val);  // Same operation for both directions
}

U32 collab_ntoh32(U32 val) {
    return collab_hton32(val);
}

U64 collab_ntoh64(U64 val) {
    return collab_hton64(val);
}

// Write integers to buffer in network byte order (big-endian)
void collab_write_u16(U8 *buf, U16 val) {
    buf[0] = (U8)(val >> 8);
    buf[1] = (U8)(val & 0xFF);
}

void collab_write_u32(U8 *buf, U32 val) {
    buf[0] = (U8)(val >> 24);
    buf[1] = (U8)((val >> 16) & 0xFF);
    buf[2] = (U8)((val >> 8) & 0xFF);
    buf[3] = (U8)(val & 0xFF);
}

void collab_write_u64(U8 *buf, U64 val) {
    buf[0] = (U8)(val >> 56);
    buf[1] = (U8)((val >> 48) & 0xFF);
    buf[2] = (U8)((val >> 40) & 0xFF);
    buf[3] = (U8)((val >> 32) & 0xFF);
    buf[4] = (U8)((val >> 24) & 0xFF);
    buf[5] = (U8)((val >> 16) & 0xFF);
    buf[6] = (U8)((val >> 8) & 0xFF);
    buf[7] = (U8)(val & 0xFF);
}

// Read integers from buffer in network byte order (big-endian)
U16 collab_read_u16(const U8 *buf) {
    return (U16)((buf[0] << 8) | buf[1]);
}

U32 collab_read_u32(const U8 *buf) {
    return ((U32)buf[0] << 24) |
           ((U32)buf[1] << 16) |
           ((U32)buf[2] << 8) |
           (U32)buf[3];
}

U64 collab_read_u64(const U8 *buf) {
    return ((U64)buf[0] << 56) |
           ((U64)buf[1] << 48) |
           ((U64)buf[2] << 40) |
           ((U64)buf[3] << 32) |
           ((U64)buf[4] << 24) |
           ((U64)buf[5] << 16) |
           ((U64)buf[6] << 8) |
           (U64)buf[7];
}

// Need errno.h for error code constants
#include <errno.h>
#include <netdb.h>  // for EAI_NONAME

// User-friendly error messages for common network errors
const char *collab_error_user_friendly(int errcode) {
    switch (errcode) {
        case ECONNREFUSED:
            return "Connection refused - server may not be running";
        case ETIMEDOUT:
            return "Connection timed out - check network connectivity";
        case EADDRINUSE:
            return "Port is already in use - try another port";
        case EADDRNOTAVAIL:
            return "Address not available - check server address";
        case ENETUNREACH:
            return "Network unreachable - check your network connection";
        case EHOSTUNREACH:
            return "Host unreachable - server may be offline";
        case ECONNRESET:
            return "Connection reset by server";
        case EPIPE:
            return "Connection closed by server";
        case ENOENT:
            return "Hostname not found - check server address";
        case EACCES:
            return "Permission denied - need admin rights for ports < 1024";
        case EMFILE:
        case ENFILE:
            return "Too many open files - system limit reached";
        case ENOBUFS:
        case ENOMEM:
            return "Out of memory";
        default:
            return NULL;  // Fall back to system error message
    }
}

// Set error with user-friendly message based on errno
void collab_error_set_network(Collab_error *err, int errcode,
                               const char *operation, const char *host, U16 port) {
    const char *friendly = collab_error_user_friendly(errcode);

    if (friendly) {
        if (host && port > 0) {
            snprintf(err->message, sizeof(err->message),
                     "Cannot %s %s:%u\n%s",
                     operation, host, (unsigned)port, friendly);
        } else if (port > 0) {
            snprintf(err->message, sizeof(err->message),
                     "Cannot %s on port %u\n%s",
                     operation, (unsigned)port, friendly);
        } else {
            snprintf(err->message, sizeof(err->message),
                     "Cannot %s: %s",
                     operation, friendly);
        }
    } else {
        // Fall back to system error message
        if (host && port > 0) {
            snprintf(err->message, sizeof(err->message),
                     "Cannot %s %s:%u\n%s",
                     operation, host, (unsigned)port, strerror(errcode));
        } else if (port > 0) {
            snprintf(err->message, sizeof(err->message),
                     "Cannot %s on port %u\n%s",
                     operation, (unsigned)port, strerror(errcode));
        } else {
            snprintf(err->message, sizeof(err->message),
                     "Cannot %s: %s",
                     operation, strerror(errcode));
        }
    }
    err->code = errcode;
}
