#!/bin/bash
# Test playback synchronization (Phase 5)
# This script tests the playback sync messages

set -e

echo "=== Phase 5: Playback Synchronization Test ==="
echo ""

# Build the project
echo "Building..."
cd /workspace/Orca-c-collab
./tool build orca 2>&1 | tail -5

# Create a simple test file
mkdir -p /tmp/orca_test

echo ""
echo "=== Test 1: Server start/stop playback ==="
echo "This test verifies that play state messages are properly serialized."
echo ""

# Run a quick unit test of the protocol
cat > /tmp/orca_test/test_protocol.c << 'EOF'
#include <stdio.h>
#include <string.h>
#include <assert.h>

// Include protocol header
#include "collaboration/collab_protocol.h"
#include "collaboration/collab_util.h"

int main() {
    U8 buf[64];

    // Test MSG_PLAY_STATE serialization
    printf("Testing MSG_PLAY_STATE serialization...\n");
    Usz len = collab_serialize_play_state(buf, 1, 12345, 120, 0.5);
    assert(len == MSG_PLAY_STATE_SIZE);
    printf("  Serialized %zu bytes\n", len);

    Msg_play_state msg;
    assert(collab_deserialize_play_state(buf, len, &msg));
    assert(msg.is_playing == 1);
    assert(msg.tick_num == 12345);
    assert(msg.bpm == 120);
    // Check that accum_secs is close (floating point precision)
    assert(msg.accum_secs > 0.49 && msg.accum_secs < 0.51);
    printf("  Deserialized: playing=%d, tick=%llu, bpm=%u, accum=%f\n",
           msg.is_playing, (unsigned long long)msg.tick_num, msg.bpm, msg.accum_secs);
    printf("  PASS\n\n");

    // Test MSG_BPM_UPDATE serialization
    printf("Testing MSG_BPM_UPDATE serialization...\n");
    len = collab_serialize_bpm_update(buf, 180);
    assert(len == MSG_BPM_UPDATE_SIZE);
    printf("  Serialized %zu bytes\n", len);

    Msg_bpm_update bpm_msg;
    assert(collab_deserialize_bpm_update(buf, len, &bpm_msg));
    assert(bpm_msg.bpm == 180);
    printf("  Deserialized: bpm=%u\n", bpm_msg.bpm);
    printf("  PASS\n\n");

    // Test MSG_TICK_SYNC serialization
    printf("Testing MSG_TICK_SYNC serialization...\n");
    len = collab_serialize_tick_sync(buf, 9999, 0.123);
    assert(len == MSG_TICK_SYNC_SIZE);
    printf("  Serialized %zu bytes\n", len);

    Msg_tick_sync tick_msg;
    assert(collab_deserialize_tick_sync(buf, len, &tick_msg));
    assert(tick_msg.tick_num == 9999);
    // Check that accum_secs is close
    assert(tick_msg.accum_secs > 0.122 && tick_msg.accum_secs < 0.124);
    printf("  Deserialized: tick=%llu, accum=%f\n",
           (unsigned long long)tick_msg.tick_num, tick_msg.accum_secs);
    printf("  PASS\n\n");

    printf("All playback sync protocol tests passed!\n");
    return 0;
}
EOF

echo "Compiling protocol test..."
gcc -o /tmp/orca_test/test_protocol \
    /tmp/orca_test/test_protocol.c \
    collaboration/collab_protocol.c \
    collaboration/collab_util.c \
    -I. \
    -DCOLLAB_LOG_LEVEL=0 \
    -std=c99 \
    -Wall \
    -D_POSIX_C_SOURCE=200809L \
    2>&1

echo "Running protocol test..."
/tmp/orca_test/test_protocol

echo ""
echo "=== Test 2: Message Type Names ==="
cat > /tmp/orca_test/test_msg_names.c << 'EOF'
#include <stdio.h>
#include <string.h>
#include "collaboration/collab_protocol.h"

int main() {
    printf("MSG_PLAY_STATE (0x%02X): %s\n", MSG_PLAY_STATE, collab_msg_type_name(MSG_PLAY_STATE));
    printf("MSG_BPM_UPDATE (0x%02X): %s\n", MSG_BPM_UPDATE, collab_msg_type_name(MSG_BPM_UPDATE));
    printf("MSG_TICK_SYNC (0x%02X): %s\n", MSG_TICK_SYNC, collab_msg_type_name(MSG_TICK_SYNC));

    // Verify the names are correct
    if (strcmp(collab_msg_type_name(MSG_PLAY_STATE), "PLAY_STATE") != 0) return 1;
    if (strcmp(collab_msg_type_name(MSG_BPM_UPDATE), "BPM_UPDATE") != 0) return 1;
    if (strcmp(collab_msg_type_name(MSG_TICK_SYNC), "TICK_SYNC") != 0) return 1;

    printf("All message type names verified!\n");
    return 0;
}
EOF

echo "Compiling message names test..."
gcc -o /tmp/orca_test/test_msg_names \
    /tmp/orca_test/test_msg_names.c \
    collaboration/collab_protocol.c \
    collaboration/collab_util.c \
    -I. \
    -DCOLLAB_LOG_LEVEL=0 \
    -std=c99 \
    -Wall \
    -D_POSIX_C_SOURCE=200809L \
    2>&1

echo "Running message names test..."
/tmp/orca_test/test_msg_names

echo ""
echo "=== All Phase 5 protocol tests passed! ==="
echo ""
echo "NOTE: Full end-to-end testing requires manual verification:"
echo "  1. Start server: ./build/orca --server 7777 test.orca"
echo "  2. Start client: ./build/orca --client localhost:7777"
echo "  3. On server: press spacebar to start playback"
echo "  4. Verify client shows playback indicator"
echo "  5. On client: press spacebar (should show 'Playback controlled by server')"
echo "  6. On server: press < or > to change BPM"
echo "  7. Verify client BPM updates"
echo "  8. On server: press spacebar to stop"
echo "  9. Verify client stops"
echo ""
