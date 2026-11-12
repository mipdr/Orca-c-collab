// test_collab_protocol.c - Unit tests for collaboration protocol

#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "../collaboration/collab_protocol.h"

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

// Test magic validation
static void test_magic(void) {
    TEST("collab_validate_magic valid");

    if (collab_validate_magic(COLLAB_MAGIC)) {
        PASS();
    } else {
        FAIL("Should accept valid magic");
    }

    TEST("collab_validate_magic invalid");

    if (!collab_validate_magic(0x12345678)) {
        PASS();
    } else {
        FAIL("Should reject invalid magic");
    }
}

// Test header serialization
static void test_header(void) {
    TEST("collab_serialize_header");

    U8 buf[COLLAB_HEADER_SIZE];
    Usz written = collab_serialize_header(buf, MSG_HEARTBEAT, 0);

    if (written == COLLAB_HEADER_SIZE) {
        PASS();
    } else {
        FAIL("Wrong header size");
    }

    TEST("collab_parse_header");

    Collab_msg_header header;
    if (collab_parse_header(buf, &header)) {
        if (header.magic == COLLAB_MAGIC &&
            header.length == 0 &&
            header.msg_type == MSG_HEARTBEAT) {
            PASS();
        } else {
            FAIL("Header fields incorrect");
        }
    } else {
        FAIL("Parse failed");
    }

    TEST("collab_parse_header with payload");

    collab_serialize_header(buf, MSG_CLIENT_JOIN, 100);
    if (collab_parse_header(buf, &header)) {
        if (header.length == 100 && header.msg_type == MSG_CLIENT_JOIN) {
            PASS();
        } else {
            FAIL("Payload length incorrect");
        }
    } else {
        FAIL("Parse failed");
    }
}

// Test CLIENT_JOIN serialization
static void test_client_join(void) {
    TEST("collab_serialize_client_join");

    U8 buf[128];
    Usz len = collab_serialize_client_join(buf, "TestUser");

    if (len == 1 + COLLAB_MAX_CLIENT_NAME) {
        PASS();
    } else {
        FAIL("Wrong payload size");
    }

    TEST("collab_deserialize_client_join");

    Msg_client_join msg;
    if (collab_deserialize_client_join(buf, len, &msg)) {
        if (msg.protocol_version == COLLAB_PROTOCOL_VERSION &&
            strcmp(msg.client_name, "TestUser") == 0) {
            PASS();
        } else {
            FAIL("Deserialized values incorrect");
        }
    } else {
        FAIL("Deserialize failed");
    }

    TEST("client_join long name truncation");

    const char *long_name = "ThisIsAVeryLongNameThatShouldBeTruncated!!";
    collab_serialize_client_join(buf, long_name);
    if (collab_deserialize_client_join(buf, len, &msg)) {
        if (strlen(msg.client_name) < strlen(long_name)) {
            PASS();
        } else {
            FAIL("Name should be truncated");
        }
    } else {
        FAIL("Deserialize failed");
    }
}

// Test CLIENT_ACCEPT serialization
static void test_client_accept(void) {
    TEST("collab_serialize_client_accept");

    U8 buf[16];
    Usz len = collab_serialize_client_accept(buf, 5, 3, 10);

    if (len == 4) {
        PASS();
    } else {
        FAIL("Wrong payload size");
    }

    TEST("collab_deserialize_client_accept");

    Msg_client_accept msg;
    if (collab_deserialize_client_accept(buf, len, &msg)) {
        if (msg.client_id == 5 &&
            msg.color_index == 3 &&
            msg.num_clients == 10) {
            PASS();
        } else {
            FAIL("Deserialized values incorrect");
        }
    } else {
        FAIL("Deserialize failed");
    }
}

// Test CLIENT_REJECT serialization
static void test_client_reject(void) {
    TEST("collab_serialize_client_reject");

    U8 buf[128];
    Usz len = collab_serialize_client_reject(buf, REJECT_SERVER_FULL, "Server is full");

    if (len == 65) {
        PASS();
    } else {
        FAIL("Wrong payload size");
    }

    TEST("collab_deserialize_client_reject");

    Msg_client_reject msg;
    if (collab_deserialize_client_reject(buf, len, &msg)) {
        if (msg.reason_code == REJECT_SERVER_FULL &&
            strcmp(msg.reason_text, "Server is full") == 0) {
            PASS();
        } else {
            FAIL("Deserialized values incorrect");
        }
    } else {
        FAIL("Deserialize failed");
    }
}

// Test GRID_WRITE serialization
static void test_grid_write(void) {
    TEST("collab_serialize_grid_write");

    U8 buf[16];
    Usz len = collab_serialize_grid_write(buf, 3, 100, 200, 'A');

    if (len == 6) {
        PASS();
    } else {
        FAIL("Wrong payload size");
    }

    TEST("collab_deserialize_grid_write");

    Msg_grid_write msg;
    if (collab_deserialize_grid_write(buf, len, &msg)) {
        if (msg.client_id == 3 &&
            msg.y == 100 &&
            msg.x == 200 &&
            msg.glyph == 'A') {
            PASS();
        } else {
            FAIL("Deserialized values incorrect");
        }
    } else {
        FAIL("Deserialize failed");
    }
}

// Test CURSOR_UPDATE serialization
static void test_cursor_update(void) {
    TEST("collab_serialize_cursor_update");

    U8 buf[16];
    Usz len = collab_serialize_cursor_update(buf, 2, 50, 75, 10, 20);

    if (len == 9) {
        PASS();
    } else {
        FAIL("Wrong payload size");
    }

    TEST("collab_deserialize_cursor_update");

    Msg_cursor_update msg;
    if (collab_deserialize_cursor_update(buf, len, &msg)) {
        if (msg.client_id == 2 &&
            msg.y == 50 &&
            msg.x == 75 &&
            msg.h == 10 &&
            msg.w == 20) {
            PASS();
        } else {
            FAIL("Deserialized values incorrect");
        }
    } else {
        FAIL("Deserialize failed");
    }
}

// Test CLIENT_HASH serialization
static void test_client_hash(void) {
    TEST("collab_serialize_client_hash");

    U8 buf[16];
    U64 hash = 0x123456789ABCDEF0ULL;
    Usz len = collab_serialize_client_hash(buf, 7, hash);

    if (len == 9) {
        PASS();
    } else {
        FAIL("Wrong payload size");
    }

    TEST("collab_deserialize_client_hash");

    Msg_client_hash msg;
    if (collab_deserialize_client_hash(buf, len, &msg)) {
        if (msg.client_id == 7 && msg.hash == hash) {
            PASS();
        } else {
            FAIL("Deserialized values incorrect");
        }
    } else {
        FAIL("Deserialize failed");
    }
}

// Test full message building
static void test_build_message(void) {
    TEST("collab_build_message");

    U8 payload[4];
    payload[0] = 1;
    payload[1] = 2;
    payload[2] = 3;
    payload[3] = 4;

    U8 buf[COLLAB_MAX_MESSAGE_SIZE];
    Usz total = collab_build_message(buf, MSG_HEARTBEAT, payload, 4);

    if (total == COLLAB_HEADER_SIZE + 4) {
        PASS();
    } else {
        FAIL("Wrong total size");
    }

    TEST("collab_build_message header+payload");

    Collab_msg_header header;
    if (collab_parse_header(buf, &header)) {
        if (header.length == 4 &&
            buf[COLLAB_HEADER_SIZE] == 1 &&
            buf[COLLAB_HEADER_SIZE + 3] == 4) {
            PASS();
        } else {
            FAIL("Message content incorrect");
        }
    } else {
        FAIL("Header parse failed");
    }
}

// Test message buffer
static void test_msg_buffer(void) {
    TEST("collab_msg_buffer_init");

    Collab_msg_buffer buf;
    collab_msg_buffer_init(&buf);

    if (buf.offset == 0 && !buf.has_header) {
        PASS();
    } else {
        FAIL("Buffer not initialized correctly");
    }

    TEST("collab_msg_buffer_reset");

    buf.offset = 100;
    buf.has_header = true;
    collab_msg_buffer_reset(&buf);

    if (buf.offset == 0 && !buf.has_header) {
        PASS();
    } else {
        FAIL("Buffer not reset correctly");
    }
}

// Test message type names
static void test_msg_type_names(void) {
    TEST("collab_msg_type_name");

    if (strcmp(collab_msg_type_name(MSG_HEARTBEAT), "HEARTBEAT") == 0 &&
        strcmp(collab_msg_type_name(MSG_CLIENT_JOIN), "CLIENT_JOIN") == 0 &&
        strcmp(collab_msg_type_name(MSG_GRID_WRITE), "GRID_WRITE") == 0) {
        PASS();
    } else {
        FAIL("Type names incorrect");
    }
}

// Test deserialize failure cases
static void test_deserialize_failures(void) {
    TEST("deserialize too short client_join");

    U8 buf[2] = {0, 0};
    Msg_client_join msg_join;
    if (!collab_deserialize_client_join(buf, 2, &msg_join)) {
        PASS();
    } else {
        FAIL("Should fail on short buffer");
    }

    TEST("deserialize too short grid_write");

    Msg_grid_write msg_write;
    if (!collab_deserialize_grid_write(buf, 2, &msg_write)) {
        PASS();
    } else {
        FAIL("Should fail on short buffer");
    }

    TEST("deserialize too short cursor_update");

    Msg_cursor_update msg_cursor;
    if (!collab_deserialize_cursor_update(buf, 2, &msg_cursor)) {
        PASS();
    } else {
        FAIL("Should fail on short buffer");
    }

    TEST("deserialize too short client_hash");

    Msg_client_hash msg_hash;
    if (!collab_deserialize_client_hash(buf, 2, &msg_hash)) {
        PASS();
    } else {
        FAIL("Should fail on short buffer");
    }
}

int main(void) {
    printf("Running collab_protocol tests...\n\n");

    test_magic();
    test_header();
    test_client_join();
    test_client_accept();
    test_client_reject();
    test_grid_write();
    test_cursor_update();
    test_client_hash();
    test_build_message();
    test_msg_buffer();
    test_msg_type_names();
    test_deserialize_failures();

    printf("\n=================================\n");
    printf("Tests passed: %d\n", tests_passed);
    printf("Tests failed: %d\n", tests_failed);
    printf("=================================\n");

    return tests_failed > 0 ? 1 : 0;
}
