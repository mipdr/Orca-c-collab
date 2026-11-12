// collab_sync.h - Grid state synchronization
// Hash computation, full sync, incremental writes

#pragma once
#include "../base.h"
#include "../field.h"
#include "collab_util.h"
#include "collab_protocol.h"

// Grid synchronization constants
#define COLLAB_HASH_INTERVAL_MS 5000  // How often to send hash verification
#define COLLAB_MAX_BATCH_WRITES 1024  // Maximum writes in a batch

// Single write tuple for batch operations
typedef struct {
    U16 y;
    U16 x;
    Glyph glyph;
} Collab_write_tuple;

// MSG_GRID_FULL_SYNC payload structure
// Wire format:
//   [0-1]:   width (U16)
//   [2-3]:   height (U16)
//   [4-11]:  tick_num (U64)
//   [12]:    is_playing (U8)
//   [13-14]: bpm (U16)
//   [15+]:   grid data (width * height bytes)
typedef struct {
    U16 width;
    U16 height;
    U64 tick_num;
    U8 is_playing;
    U16 bpm;
    // Followed by grid data
} Msg_grid_full_sync_header;

#define MSG_GRID_FULL_SYNC_HEADER_SIZE 15

// MSG_GRID_WRITE_BATCH payload structure
// Wire format:
//   [0]:     client_id (U8)
//   [1-2]:   num_writes (U16)
//   [3+]:    write tuples (5 bytes each: U16 y, U16 x, U8 glyph)
typedef struct {
    U8 client_id;
    U16 num_writes;
    Collab_write_tuple *writes;  // Points to caller-provided buffer
} Msg_grid_write_batch;

#define MSG_GRID_WRITE_BATCH_HEADER_SIZE 3
#define MSG_GRID_WRITE_TUPLE_SIZE 5

// MSG_RESYNC_REQUEST payload (empty - just header)
// Sent by server when client hash doesn't match

// Hash cache to avoid recomputing when grid unchanged
typedef struct {
    U64 cached_hash;
    bool is_valid;
} Collab_hash_cache;

// Initialize hash cache
void collab_hash_cache_init(Collab_hash_cache *cache);

// Invalidate hash cache (call when grid is modified)
void collab_hash_cache_invalidate(Collab_hash_cache *cache);

// Compute grid hash (uses cache if valid)
U64 collab_compute_grid_hash(const Field *field, Collab_hash_cache *cache);

// Compute grid hash without cache
U64 collab_compute_grid_hash_direct(const Field *field);

// Serialize MSG_GRID_FULL_SYNC
// Returns total payload size (header + grid data)
// Buffer must be large enough: MSG_GRID_FULL_SYNC_HEADER_SIZE + width * height
Usz collab_serialize_grid_full_sync(U8 *buf, const Field *field,
                                     U64 tick_num, bool is_playing, U16 bpm);

// Deserialize MSG_GRID_FULL_SYNC header
// Returns true if successful
bool collab_deserialize_grid_full_sync_header(const U8 *buf, Usz len,
                                               Msg_grid_full_sync_header *header);

// Apply full grid sync to field
// Resizes field if necessary
// grid_data points to the raw grid bytes after the header
bool collab_apply_full_sync(Field *field, const Msg_grid_full_sync_header *header,
                            const U8 *grid_data, Usz grid_data_len);

// Serialize MSG_GRID_WRITE_BATCH
// Returns payload size
// Buffer must be large enough: MSG_GRID_WRITE_BATCH_HEADER_SIZE + num_writes * 5
Usz collab_serialize_grid_write_batch(U8 *buf, U8 client_id,
                                       const Collab_write_tuple *writes,
                                       U16 num_writes);

// Deserialize MSG_GRID_WRITE_BATCH header
// Returns true if header is valid
// Caller must then read write tuples from buf + MSG_GRID_WRITE_BATCH_HEADER_SIZE
bool collab_deserialize_grid_write_batch_header(const U8 *buf, Usz len,
                                                 U8 *client_id, U16 *num_writes);

// Parse a single write tuple from buffer
// Returns true if successful
bool collab_parse_write_tuple(const U8 *buf, Collab_write_tuple *write);

// Apply single write to field
// Returns true if write was within bounds
bool collab_apply_write(Field *field, U16 y, U16 x, Glyph glyph);

// Apply batch of writes to field
// Returns number of writes applied (may be less than num_writes if out of bounds)
Usz collab_apply_write_batch(Field *field, const Collab_write_tuple *writes,
                              Usz num_writes);

// Serialize MSG_RESYNC_REQUEST (empty payload)
Usz collab_serialize_resync_request(U8 *buf);

// Compare two grids for equality
bool collab_grids_equal(const Field *a, const Field *b);

// Copy grid from src to dest
// Resizes dest if necessary
void collab_copy_grid(const Field *src, Field *dest);

// Fill grid with a character
void collab_fill_grid(Field *field, Glyph fill_char);
