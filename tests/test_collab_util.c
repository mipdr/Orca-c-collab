// test_collab_util.c - Unit tests for collaboration utilities

#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "../collaboration/collab_util.h"

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) \
    printf("  Testing: %s...", name); \
    fflush(stdout)

#define PASS() \
    printf(" PASSED\n"); \
    tests_passed++

#define FAIL(msg) \
    printf(" FAILED: %s\n", msg); \
    tests_failed++

// Test time functions
static void test_time_ms(void) {
    TEST("collab_time_ms");

    U64 t1 = collab_time_ms();
    assert(t1 > 0);

    // Wait a bit and check time increases
    for (volatile int i = 0; i < 1000000; i++);
    U64 t2 = collab_time_ms();

    if (t2 >= t1) {
        PASS();
    } else {
        FAIL("Time should increase");
    }
}

// Test byte order functions
static void test_byte_order(void) {
    TEST("collab_hton16/ntoh16");

    U16 val16 = 0x1234;
    U16 net16 = collab_hton16(val16);
    U16 host16 = collab_ntoh16(net16);

    if (host16 == val16) {
        PASS();
    } else {
        FAIL("Round-trip failed");
    }

    TEST("collab_hton32/ntoh32");

    U32 val32 = 0x12345678;
    U32 net32 = collab_hton32(val32);
    U32 host32 = collab_ntoh32(net32);

    if (host32 == val32) {
        PASS();
    } else {
        FAIL("Round-trip failed");
    }

    TEST("collab_hton64/ntoh64");

    U64 val64 = 0x123456789ABCDEF0ULL;
    U64 net64 = collab_hton64(val64);
    U64 host64 = collab_ntoh64(net64);

    if (host64 == val64) {
        PASS();
    } else {
        FAIL("Round-trip failed");
    }
}

// Test buffer read/write functions
static void test_buffer_rw(void) {
    TEST("collab_write/read_u16");

    U8 buf[8];
    U16 val16 = 0xABCD;
    collab_write_u16(buf, val16);
    U16 read16 = collab_read_u16(buf);

    if (read16 == val16) {
        PASS();
    } else {
        FAIL("Write/read mismatch");
    }

    TEST("collab_write/read_u32");

    U32 val32 = 0xDEADBEEF;
    collab_write_u32(buf, val32);
    U32 read32 = collab_read_u32(buf);

    if (read32 == val32) {
        PASS();
    } else {
        FAIL("Write/read mismatch");
    }

    TEST("collab_write/read_u64");

    U64 val64 = 0xCAFEBABE12345678ULL;
    collab_write_u64(buf, val64);
    U64 read64 = collab_read_u64(buf);

    if (read64 == val64) {
        PASS();
    } else {
        FAIL("Write/read mismatch");
    }

    // Test big-endian encoding
    TEST("big-endian encoding");

    U32 test_val = 0x01020304;
    collab_write_u32(buf, test_val);

    if (buf[0] == 0x01 && buf[1] == 0x02 && buf[2] == 0x03 && buf[3] == 0x04) {
        PASS();
    } else {
        FAIL("Not big-endian");
    }
}

// Test hash function
static void test_hash(void) {
    TEST("collab_hash64");

    const char *data1 = "Hello, World!";
    const char *data2 = "Hello, World!";
    const char *data3 = "Hello, Orca!";

    U64 hash1 = collab_hash64(data1, strlen(data1));
    U64 hash2 = collab_hash64(data2, strlen(data2));
    U64 hash3 = collab_hash64(data3, strlen(data3));

    if (hash1 == hash2 && hash1 != hash3) {
        PASS();
    } else {
        FAIL("Hash consistency check failed");
    }

    TEST("collab_hash64 empty input");

    U64 empty_hash = collab_hash64("", 0);
    if (empty_hash != 0) {  // xxHash64 with seed 0 and empty input produces non-zero
        PASS();
    } else {
        FAIL("Empty hash should be non-zero");
    }
}

// Test error handling
static void test_error(void) {
    TEST("collab_error_set/is_set");

    Collab_error err;
    collab_error_clear(&err);

    if (!collab_error_is_set(&err)) {
        collab_error_set(&err, 42, "Test error: %d", 123);
        if (collab_error_is_set(&err) && err.code == 42) {
            PASS();
        } else {
            FAIL("Error not set correctly");
        }
    } else {
        FAIL("Error should be clear");
    }

    TEST("collab_error_clear");

    collab_error_clear(&err);
    if (!collab_error_is_set(&err) && err.code == 0) {
        PASS();
    } else {
        FAIL("Error not cleared");
    }
}

// Test logging (just make sure it doesn't crash)
static void test_logging(void) {
    TEST("collab_log functions");

    collab_log_set_level(COLLAB_LOG_DEBUG);
    COLLAB_LOG_DEBUG("Debug message: %d", 1);
    COLLAB_LOG_INFO("Info message: %d", 2);
    COLLAB_LOG_WARN("Warn message: %d", 3);
    COLLAB_LOG_ERROR("Error message: %d", 4);

    // Restore default log level
    collab_log_set_level(COLLAB_LOG_INFO);

    PASS();
}

int main(void) {
    printf("Running collab_util tests...\n\n");

    test_time_ms();
    test_byte_order();
    test_buffer_rw();
    test_hash();
    test_error();
    test_logging();

    printf("\n=================================\n");
    printf("Tests passed: %d\n", tests_passed);
    printf("Tests failed: %d\n", tests_failed);
    printf("=================================\n");

    return tests_failed > 0 ? 1 : 0;
}
