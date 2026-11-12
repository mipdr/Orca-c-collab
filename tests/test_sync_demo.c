// test_sync_demo.c - Demonstrates grid synchronization
// A standalone demo that shows server and clients syncing

#define _DEFAULT_SOURCE
#define _BSD_SOURCE

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "../collaboration/collab_server.h"
#include "../collaboration/collab_client.h"
#include "../gbuffer.h"

// Print a section of the grid for debugging
static void print_grid_section(const Field *field, int start_y, int start_x, int rows, int cols) {
    printf("  Grid (%ux%u):\n", field->width, field->height);
    for (int y = start_y; y < start_y + rows && y < (int)field->height; y++) {
        printf("    ");
        for (int x = start_x; x < start_x + cols && x < (int)field->width; x++) {
            Glyph g = field->buffer[y * field->width + x];
            printf("%c", g);
        }
        printf("\n");
    }
}

// Run updates for server and clients
static void run_updates(Collab_server *srv, Collab_client *clients[], int num_clients, int iterations) {
    for (int i = 0; i < iterations; i++) {
        if (srv) {
            collab_server_update(srv);
        }
        for (int c = 0; c < num_clients; c++) {
            if (clients[c]) {
                collab_client_update(clients[c]);
            }
        }
        usleep(10000);  // 10ms
    }
}

int main(void) {
    printf("===========================================\n");
    printf("Grid Synchronization Demo\n");
    printf("===========================================\n\n");

    // Enable debug logging
    collab_log_set_level(COLLAB_LOG_INFO);

    // ---- Step 1: Start server with initial grid ----
    printf("1. Starting server with 'ORCA' in grid...\n");

    Collab_server srv;
    collab_server_init(&srv);

    Field initial;
    field_init_fill(&initial, 16, 32, '.');

    // Write "ORCA" at position (5, 5)
    gbuffer_poke(initial.buffer, initial.height, initial.width, 5, 5, 'O');
    gbuffer_poke(initial.buffer, initial.height, initial.width, 5, 6, 'R');
    gbuffer_poke(initial.buffer, initial.height, initial.width, 5, 7, 'C');
    gbuffer_poke(initial.buffer, initial.height, initial.width, 5, 8, 'A');

    if (!collab_server_start(&srv, 19999, &initial)) {
        printf("ERROR: Failed to start server: %s\n", collab_server_get_error(&srv));
        return 1;
    }

    printf("   Server grid:\n");
    print_grid_section(collab_server_get_grid(&srv), 4, 4, 4, 8);

    // ---- Step 2: Connect Client 1 ----
    printf("\n2. Connecting Client 1 (Alice)...\n");

    Collab_client alice;
    collab_client_init(&alice);

    if (!collab_client_connect(&alice, "127.0.0.1", 19999, "Alice")) {
        printf("ERROR: Alice failed to connect: %s\n", collab_client_get_error(&alice));
        collab_server_deinit(&srv);
        field_deinit(&initial);
        return 1;
    }

    Collab_client *clients[] = { &alice };
    run_updates(&srv, clients, 1, 50);

    if (collab_client_is_active(&alice)) {
        printf("   Alice connected successfully (id=%u, color=%u)\n",
               collab_client_get_id(&alice), collab_client_get_color(&alice));
        printf("   Alice's grid:\n");
        print_grid_section(collab_client_get_grid(&alice), 4, 4, 4, 8);
    } else {
        printf("ERROR: Alice failed to become active\n");
    }

    // ---- Step 3: Alice writes "COLLAB" ----
    printf("\n3. Alice writes 'COLLAB' at (7, 5)...\n");

    collab_client_write(&alice, 7, 5, 'C');
    collab_client_write(&alice, 7, 6, 'O');
    collab_client_write(&alice, 7, 7, 'L');
    collab_client_write(&alice, 7, 8, 'L');
    collab_client_write(&alice, 7, 9, 'A');
    collab_client_write(&alice, 7, 10, 'B');

    run_updates(&srv, clients, 1, 30);

    printf("   Server grid after Alice's write:\n");
    print_grid_section(collab_server_get_grid(&srv), 4, 4, 6, 10);

    // ---- Step 4: Connect Client 2 ----
    printf("\n4. Connecting Client 2 (Bob)...\n");

    Collab_client bob;
    collab_client_init(&bob);

    if (!collab_client_connect(&bob, "127.0.0.1", 19999, "Bob")) {
        printf("ERROR: Bob failed to connect: %s\n", collab_client_get_error(&bob));
        collab_client_deinit(&alice);
        collab_server_deinit(&srv);
        field_deinit(&initial);
        return 1;
    }

    Collab_client *clients2[] = { &alice, &bob };
    run_updates(&srv, clients2, 2, 50);

    if (collab_client_is_active(&bob)) {
        printf("   Bob connected successfully (id=%u, color=%u)\n",
               collab_client_get_id(&bob), collab_client_get_color(&bob));
        printf("   Bob should see both 'ORCA' and 'COLLAB':\n");
        print_grid_section(collab_client_get_grid(&bob), 4, 4, 6, 10);
    } else {
        printf("ERROR: Bob failed to become active\n");
    }

    // ---- Step 5: Bob writes, verify Alice sees it ----
    printf("\n5. Bob writes 'SYNC' at (9, 5)...\n");

    collab_client_write(&bob, 9, 5, 'S');
    collab_client_write(&bob, 9, 6, 'Y');
    collab_client_write(&bob, 9, 7, 'N');
    collab_client_write(&bob, 9, 8, 'C');

    run_updates(&srv, clients2, 2, 30);

    printf("   Alice should see Bob's write:\n");
    print_grid_section(collab_client_get_grid(&alice), 4, 4, 8, 10);

    // ---- Step 6: Server writes, verify both clients see it ----
    printf("\n6. Server writes 'HOST' at (11, 5)...\n");

    collab_server_write(&srv, 11, 5, 'H');
    collab_server_write(&srv, 11, 6, 'O');
    collab_server_write(&srv, 11, 7, 'S');
    collab_server_write(&srv, 11, 8, 'T');

    run_updates(&srv, clients2, 2, 30);

    printf("   Both clients should see server's write:\n");
    printf("   Alice:\n");
    print_grid_section(collab_client_get_grid(&alice), 4, 4, 10, 10);
    printf("   Bob:\n");
    print_grid_section(collab_client_get_grid(&bob), 4, 4, 10, 10);

    // ---- Step 7: Batch write ----
    printf("\n7. Alice does batch write '12345' at (13, 5)...\n");

    Collab_write_tuple batch[5] = {
        {.y = 13, .x = 5, .glyph = '1'},
        {.y = 13, .x = 6, .glyph = '2'},
        {.y = 13, .x = 7, .glyph = '3'},
        {.y = 13, .x = 8, .glyph = '4'},
        {.y = 13, .x = 9, .glyph = '5'},
    };

    collab_client_write_batch(&alice, batch, 5);
    run_updates(&srv, clients2, 2, 30);

    printf("   Bob should see batch write:\n");
    print_grid_section(collab_client_get_grid(&bob), 4, 4, 12, 10);

    // ---- Step 8: Verify final state ----
    printf("\n8. Verifying all grids are synchronized...\n");

    const Field *srv_grid = collab_server_get_grid(&srv);
    const Field *alice_grid = collab_client_get_grid_const(&alice);
    const Field *bob_grid = collab_client_get_grid_const(&bob);

    bool alice_match = collab_grids_equal(srv_grid, alice_grid);
    bool bob_match = collab_grids_equal(srv_grid, bob_grid);

    if (alice_match && bob_match) {
        printf("   SUCCESS: All grids are synchronized!\n");
    } else {
        printf("   FAILURE: Grids do not match (alice=%d, bob=%d)\n", alice_match, bob_match);
    }

    // ---- Step 9: Hash verification ----
    printf("\n9. Testing hash verification...\n");

    U64 srv_hash = collab_compute_grid_hash_direct(srv_grid);
    U64 alice_hash = collab_compute_grid_hash_direct(alice_grid);
    U64 bob_hash = collab_compute_grid_hash_direct(bob_grid);

    printf("   Server hash:  0x%llx\n", (unsigned long long)srv_hash);
    printf("   Alice hash:   0x%llx\n", (unsigned long long)alice_hash);
    printf("   Bob hash:     0x%llx\n", (unsigned long long)bob_hash);

    if (srv_hash == alice_hash && srv_hash == bob_hash) {
        printf("   SUCCESS: All hashes match!\n");
    } else {
        printf("   FAILURE: Hashes do not match\n");
    }

    // ---- Cleanup ----
    printf("\n10. Cleaning up...\n");

    collab_client_disconnect(&alice);
    collab_client_disconnect(&bob);
    run_updates(&srv, NULL, 0, 10);

    collab_client_deinit(&alice);
    collab_client_deinit(&bob);
    collab_server_deinit(&srv);
    field_deinit(&initial);

    printf("   Done!\n");
    printf("\n===========================================\n");
    printf("Demo completed successfully!\n");
    printf("===========================================\n");

    return 0;
}
