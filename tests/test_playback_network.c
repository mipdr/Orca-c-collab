// Test playback synchronization over the network
// This test creates a server and client, then tests playback state sync

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>

#include "collaboration/collab_server.h"
#include "collaboration/collab_client.h"
#include "collaboration/collab_util.h"

// Test utility: wait for condition with timeout
#define WAIT_FOR(cond, max_ms) do { \
    int _elapsed = 0; \
    while (!(cond) && _elapsed < (max_ms)) { \
        usleep(10000); \
        _elapsed += 10; \
    } \
} while(0)

int main() {
    printf("=== Network Playback Sync Test ===\n\n");

    // Create a simple grid
    Field grid;
    field_init_fill(&grid, 8, 8, '.');

    // Initialize server
    printf("1. Starting server on port 7778...\n");
    Collab_server srv;
    collab_server_init(&srv);
    assert(collab_server_start(&srv, 7778, &grid));
    printf("   Server started\n");

    // Initialize client
    printf("2. Connecting client...\n");
    Collab_client client;
    collab_client_init(&client);
    assert(collab_client_connect(&client, "127.0.0.1", 7778, "TestClient"));

    // Wait for connection
    for (int i = 0; i < 100; i++) {
        collab_server_update(&srv);
        collab_client_update(&client);
        usleep(10000);
        if (collab_client_get_state(&client) == COLLAB_CLIENT_ACTIVE) break;
    }
    assert(collab_client_get_state(&client) == COLLAB_CLIENT_ACTIVE);
    printf("   Client connected and active\n");

    // Test 1: Send play state
    printf("3. Testing MSG_PLAY_STATE broadcast...\n");
    collab_server_set_playing(&srv, true, 100, 0.25);

    // Wait for client to receive
    for (int i = 0; i < 50; i++) {
        collab_server_update(&srv);
        collab_client_update(&client);
        usleep(10000);
        if (collab_client_playback_changed(&client)) break;
    }

    // Check client state
    bool is_playing;
    U64 tick_num;
    U16 bpm;
    double accum_secs;
    assert(collab_client_get_playback_state(&client, &is_playing, &tick_num, &bpm, &accum_secs));
    assert(is_playing == true);
    assert(tick_num == 100);
    assert(bpm == 120);  // Server's initial BPM
    printf("   Play state received: playing=%d, tick=%llu, bpm=%u\n",
           is_playing, (unsigned long long)tick_num, bpm);

    // Test 2: BPM update
    printf("4. Testing MSG_BPM_UPDATE broadcast...\n");
    collab_server_set_bpm(&srv, 180);

    // Wait for client to receive
    for (int i = 0; i < 50; i++) {
        collab_server_update(&srv);
        collab_client_update(&client);
        usleep(10000);
        if (collab_client_playback_changed(&client)) break;
    }

    assert(collab_client_get_playback_state(&client, NULL, NULL, &bpm, NULL));
    assert(bpm == 180);
    printf("   BPM updated to %u\n", bpm);

    // Test 3: Tick sync
    printf("5. Testing MSG_TICK_SYNC broadcast...\n");
    collab_server_set_tick(&srv, 500, 0.5);
    collab_server_broadcast_tick_sync(&srv);

    // Wait for client to receive
    for (int i = 0; i < 50; i++) {
        collab_server_update(&srv);
        collab_client_update(&client);
        usleep(10000);
        if (collab_client_playback_changed(&client)) break;
    }

    assert(collab_client_get_playback_state(&client, NULL, &tick_num, NULL, &accum_secs));
    assert(tick_num == 500);
    printf("   Tick synced to %llu, accum=%f\n", (unsigned long long)tick_num, accum_secs);

    // Test 4: Stop playback
    printf("6. Testing play stop...\n");
    collab_server_set_playing(&srv, false, 550, 0.0);

    // Wait for client to receive
    for (int i = 0; i < 50; i++) {
        collab_server_update(&srv);
        collab_client_update(&client);
        usleep(10000);
        if (collab_client_playback_changed(&client)) break;
    }

    assert(collab_client_get_playback_state(&client, &is_playing, &tick_num, NULL, NULL));
    assert(is_playing == false);
    assert(tick_num == 550);
    printf("   Playback stopped at tick %llu\n", (unsigned long long)tick_num);

    // Cleanup
    printf("7. Cleaning up...\n");
    collab_client_disconnect(&client);
    collab_client_deinit(&client);
    collab_server_stop(&srv);
    collab_server_deinit(&srv);
    field_deinit(&grid);

    printf("\n=== All network playback tests passed! ===\n");
    return 0;
}
