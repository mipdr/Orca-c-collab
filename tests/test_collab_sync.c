// test_collab_sync.c - Unit tests for grid synchronization

#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "../collaboration/collab_sync.h"
#include "../field.h"
#include "../gbuffer.h"

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

// Test hash computation
static void test_hash_computation(void) {
    TEST("collab_compute_grid_hash_direct");

    Field field;
    field_init_fill(&field, 32, 64, '.');

    U64 hash1 = collab_compute_grid_hash_direct(&field);
    U64 hash2 = collab_compute_grid_hash_direct(&field);

    if (hash1 == hash2 && hash1 != 0) {
        PASS();
    } else {
        FAIL("Hash should be consistent and non-zero");
    }

    // Modify grid and verify hash changes
    TEST("hash changes with grid modification");

    gbuffer_poke(field.buffer, field.height, field.width, 10, 20, 'A');
    U64 hash3 = collab_compute_grid_hash_direct(&field);

    if (hash3 != hash1) {
        PASS();
    } else {
        FAIL("Hash should change after modification");
    }

    field_deinit(&field);
}

// Test hash cache
static void test_hash_cache(void) {
    TEST("collab_hash_cache_init");

    Collab_hash_cache cache;
    collab_hash_cache_init(&cache);

    if (!cache.is_valid && cache.cached_hash == 0) {
        PASS();
    } else {
        FAIL("Cache should be invalid initially");
    }

    TEST("collab_compute_grid_hash with cache");

    Field field;
    field_init_fill(&field, 32, 64, '.');

    U64 hash1 = collab_compute_grid_hash(&field, &cache);
    if (cache.is_valid && cache.cached_hash == hash1) {
        PASS();
    } else {
        FAIL("Cache should be valid after computation");
    }

    TEST("hash cache returns cached value");

    // Modify field without invalidating cache (to test cache behavior)
    gbuffer_poke(field.buffer, field.height, field.width, 0, 0, 'X');
    U64 hash2 = collab_compute_grid_hash(&field, &cache);

    // Should still return cached hash (not recomputed)
    if (hash2 == hash1) {
        PASS();
    } else {
        FAIL("Cache should return cached value");
    }

    TEST("collab_hash_cache_invalidate");

    collab_hash_cache_invalidate(&cache);
    U64 hash3 = collab_compute_grid_hash(&field, &cache);

    if (hash3 != hash1) {
        PASS();
    } else {
        FAIL("Hash should be recomputed after invalidation");
    }

    field_deinit(&field);
}

// Test full grid sync serialization
static void test_full_sync_serialize(void) {
    TEST("collab_serialize_grid_full_sync");

    Field field;
    field_init_fill(&field, 8, 16, '.');

    // Put some data in grid
    gbuffer_poke(field.buffer, field.height, field.width, 0, 0, 'H');
    gbuffer_poke(field.buffer, field.height, field.width, 0, 1, 'E');
    gbuffer_poke(field.buffer, field.height, field.width, 0, 2, 'L');
    gbuffer_poke(field.buffer, field.height, field.width, 0, 3, 'L');
    gbuffer_poke(field.buffer, field.height, field.width, 0, 4, 'O');

    U8 buf[MSG_GRID_FULL_SYNC_HEADER_SIZE + 16 * 8];
    Usz len = collab_serialize_grid_full_sync(buf, &field, 12345, true, 120);

    Usz expected_len = MSG_GRID_FULL_SYNC_HEADER_SIZE + 16 * 8;
    if (len == expected_len) {
        PASS();
    } else {
        FAIL("Incorrect serialized length");
    }

    TEST("collab_deserialize_grid_full_sync_header");

    Msg_grid_full_sync_header header;
    if (collab_deserialize_grid_full_sync_header(buf, len, &header)) {
        if (header.width == 16 && header.height == 8 &&
            header.tick_num == 12345 && header.is_playing == 1 &&
            header.bpm == 120) {
            PASS();
        } else {
            FAIL("Header values incorrect");
        }
    } else {
        FAIL("Failed to deserialize header");
    }

    // Test round-trip
    TEST("full sync round-trip");

    Field field2;
    field_init(&field2);

    const U8 *grid_data = buf + MSG_GRID_FULL_SYNC_HEADER_SIZE;
    Usz grid_data_len = len - MSG_GRID_FULL_SYNC_HEADER_SIZE;

    if (collab_apply_full_sync(&field2, &header, grid_data, grid_data_len)) {
        if (collab_grids_equal(&field, &field2)) {
            PASS();
        } else {
            FAIL("Grids not equal after round-trip");
        }
    } else {
        FAIL("Failed to apply full sync");
    }

    field_deinit(&field);
    field_deinit(&field2);
}

// Test write batch serialization
static void test_write_batch_serialize(void) {
    TEST("collab_serialize_grid_write_batch");

    Collab_write_tuple writes[3] = {
        {.y = 10, .x = 20, .glyph = 'A'},
        {.y = 11, .x = 21, .glyph = 'B'},
        {.y = 12, .x = 22, .glyph = 'C'},
    };

    U8 buf[MSG_GRID_WRITE_BATCH_HEADER_SIZE + 3 * MSG_GRID_WRITE_TUPLE_SIZE];
    Usz len = collab_serialize_grid_write_batch(buf, 5, writes, 3);

    Usz expected_len = MSG_GRID_WRITE_BATCH_HEADER_SIZE + 3 * MSG_GRID_WRITE_TUPLE_SIZE;
    if (len == expected_len) {
        PASS();
    } else {
        FAIL("Incorrect serialized length");
    }

    TEST("collab_deserialize_grid_write_batch_header");

    U8 client_id;
    U16 num_writes;
    if (collab_deserialize_grid_write_batch_header(buf, len, &client_id, &num_writes)) {
        if (client_id == 5 && num_writes == 3) {
            PASS();
        } else {
            FAIL("Header values incorrect");
        }
    } else {
        FAIL("Failed to deserialize header");
    }

    TEST("collab_parse_write_tuple");

    const U8 *ptr = buf + MSG_GRID_WRITE_BATCH_HEADER_SIZE;
    Collab_write_tuple read_write;

    collab_parse_write_tuple(ptr, &read_write);
    if (read_write.y == 10 && read_write.x == 20 && read_write.glyph == 'A') {
        PASS();
    } else {
        FAIL("First tuple incorrect");
    }
}

// Test apply write
static void test_apply_write(void) {
    TEST("collab_apply_write");

    Field field;
    field_init_fill(&field, 32, 64, '.');

    if (collab_apply_write(&field, 10, 20, 'X')) {
        Glyph g = field.buffer[10 * 64 + 20];
        if (g == 'X') {
            PASS();
        } else {
            FAIL("Wrong glyph written");
        }
    } else {
        FAIL("Apply write returned false");
    }

    TEST("collab_apply_write out of bounds");

    bool result = collab_apply_write(&field, 100, 200, 'Y');
    if (!result) {
        PASS();
    } else {
        FAIL("Should fail for out of bounds");
    }

    field_deinit(&field);
}

// Test apply write batch
static void test_apply_write_batch(void) {
    TEST("collab_apply_write_batch");

    Field field;
    field_init_fill(&field, 32, 64, '.');

    Collab_write_tuple writes[5] = {
        {.y = 0, .x = 0, .glyph = '1'},
        {.y = 0, .x = 1, .glyph = '2'},
        {.y = 0, .x = 2, .glyph = '3'},
        {.y = 100, .x = 100, .glyph = 'X'},  // Out of bounds
        {.y = 0, .x = 3, .glyph = '4'},
    };

    Usz applied = collab_apply_write_batch(&field, writes, 5);

    if (applied == 4) {  // 4 valid, 1 out of bounds
        if (field.buffer[0] == '1' &&
            field.buffer[1] == '2' &&
            field.buffer[2] == '3' &&
            field.buffer[3] == '4') {
            PASS();
        } else {
            FAIL("Wrong glyphs written");
        }
    } else {
        FAIL("Wrong number of writes applied");
    }

    field_deinit(&field);
}

// Test grid comparison
static void test_grids_equal(void) {
    TEST("collab_grids_equal identical");

    Field field1, field2;
    field_init_fill(&field1, 32, 64, '.');
    field_init_fill(&field2, 32, 64, '.');

    if (collab_grids_equal(&field1, &field2)) {
        PASS();
    } else {
        FAIL("Identical grids should be equal");
    }

    TEST("collab_grids_equal different content");

    gbuffer_poke(field1.buffer, field1.height, field1.width, 5, 5, 'X');

    if (!collab_grids_equal(&field1, &field2)) {
        PASS();
    } else {
        FAIL("Different grids should not be equal");
    }

    TEST("collab_grids_equal different size");

    Field field3;
    field_init_fill(&field3, 16, 32, '.');

    if (!collab_grids_equal(&field1, &field3)) {
        PASS();
    } else {
        FAIL("Different size grids should not be equal");
    }

    field_deinit(&field1);
    field_deinit(&field2);
    field_deinit(&field3);
}

// Test copy grid
static void test_copy_grid(void) {
    TEST("collab_copy_grid");

    Field src, dest;
    field_init_fill(&src, 32, 64, '.');
    field_init(&dest);

    // Put some data in source
    gbuffer_poke(src.buffer, src.height, src.width, 10, 20, 'A');
    gbuffer_poke(src.buffer, src.height, src.width, 15, 30, 'B');

    collab_copy_grid(&src, &dest);

    if (collab_grids_equal(&src, &dest)) {
        PASS();
    } else {
        FAIL("Copy not equal to source");
    }

    field_deinit(&src);
    field_deinit(&dest);
}

// Test fill grid
static void test_fill_grid(void) {
    TEST("collab_fill_grid");

    Field field;
    field_init_fill(&field, 8, 8, 'X');

    collab_fill_grid(&field, '.');

    bool all_dots = true;
    for (Usz i = 0; i < 64; i++) {
        if (field.buffer[i] != '.') {
            all_dots = false;
            break;
        }
    }

    if (all_dots) {
        PASS();
    } else {
        FAIL("Not all cells filled");
    }

    field_deinit(&field);
}

// Test edge cases
static void test_edge_cases(void) {
    TEST("empty grid hash");

    Field empty;
    field_init(&empty);
    U64 hash = collab_compute_grid_hash_direct(&empty);

    if (hash == 0) {  // Empty/null buffer should return 0
        PASS();
    } else {
        FAIL("Empty grid hash should be 0");
    }

    TEST("large grid hash");

    Field large;
    field_init_fill(&large, 256, 256, '.');

    U64 large_hash = collab_compute_grid_hash_direct(&large);
    if (large_hash != 0) {
        PASS();
    } else {
        FAIL("Large grid hash should be non-zero");
    }

    field_deinit(&large);

    TEST("boundary writes");

    Field field;
    field_init_fill(&field, 32, 64, '.');

    // Write at boundaries
    bool r1 = collab_apply_write(&field, 0, 0, 'A');  // Top-left
    bool r2 = collab_apply_write(&field, 0, 63, 'B');  // Top-right
    bool r3 = collab_apply_write(&field, 31, 0, 'C');  // Bottom-left
    bool r4 = collab_apply_write(&field, 31, 63, 'D');  // Bottom-right

    if (r1 && r2 && r3 && r4 &&
        field.buffer[0] == 'A' &&
        field.buffer[63] == 'B' &&
        field.buffer[31 * 64] == 'C' &&
        field.buffer[31 * 64 + 63] == 'D') {
        PASS();
    } else {
        FAIL("Boundary writes failed");
    }

    field_deinit(&field);
}

int main(void) {
    printf("Running collab_sync tests...\n\n");

    // Suppress log output during tests
    collab_log_set_level(COLLAB_LOG_ERROR);

    test_hash_computation();
    test_hash_cache();
    test_full_sync_serialize();
    test_write_batch_serialize();
    test_apply_write();
    test_apply_write_batch();
    test_grids_equal();
    test_copy_grid();
    test_fill_grid();
    test_edge_cases();

    printf("\n=================================\n");
    printf("Tests passed: %d\n", tests_passed);
    printf("Tests failed: %d\n", tests_failed);
    printf("=================================\n");

    return tests_failed > 0 ? 1 : 0;
}
