// collab_sync.c - Grid state synchronization
// Hash computation, full sync, incremental writes

#include "collab_sync.h"
#include "../gbuffer.h"
#include <string.h>

// Initialize hash cache
void collab_hash_cache_init(Collab_hash_cache *cache) {
    cache->cached_hash = 0;
    cache->is_valid = false;
}

// Invalidate hash cache
void collab_hash_cache_invalidate(Collab_hash_cache *cache) {
    cache->is_valid = false;
}

// Compute grid hash without cache
U64 collab_compute_grid_hash_direct(const Field *field) {
    if (!field || !field->buffer || field->width == 0 || field->height == 0) {
        return 0;
    }
    Usz size = (Usz)field->width * (Usz)field->height;
    return collab_hash64(field->buffer, size);
}

// Compute grid hash (uses cache if valid)
U64 collab_compute_grid_hash(const Field *field, Collab_hash_cache *cache) {
    if (cache && cache->is_valid) {
        return cache->cached_hash;
    }

    U64 hash = collab_compute_grid_hash_direct(field);

    if (cache) {
        cache->cached_hash = hash;
        cache->is_valid = true;
    }

    return hash;
}

// Serialize MSG_GRID_FULL_SYNC
Usz collab_serialize_grid_full_sync(U8 *buf, const Field *field,
                                     U64 tick_num, bool is_playing, U16 bpm) {
    // Write header
    collab_write_u16(buf + 0, field->width);
    collab_write_u16(buf + 2, field->height);
    collab_write_u64(buf + 4, tick_num);
    buf[12] = is_playing ? 1 : 0;
    collab_write_u16(buf + 13, bpm);

    // Write grid data
    Usz grid_size = (Usz)field->width * (Usz)field->height;
    if (field->buffer && grid_size > 0) {
        memcpy(buf + MSG_GRID_FULL_SYNC_HEADER_SIZE, field->buffer, grid_size);
    }

    return MSG_GRID_FULL_SYNC_HEADER_SIZE + grid_size;
}

// Deserialize MSG_GRID_FULL_SYNC header
bool collab_deserialize_grid_full_sync_header(const U8 *buf, Usz len,
                                               Msg_grid_full_sync_header *header) {
    if (len < MSG_GRID_FULL_SYNC_HEADER_SIZE) {
        return false;
    }

    header->width = collab_read_u16(buf + 0);
    header->height = collab_read_u16(buf + 2);
    header->tick_num = collab_read_u64(buf + 4);
    header->is_playing = buf[12];
    header->bpm = collab_read_u16(buf + 13);

    // Validate dimensions
    if (header->width == 0 || header->height == 0) {
        return false;
    }

    return true;
}

// Apply full grid sync to field
bool collab_apply_full_sync(Field *field, const Msg_grid_full_sync_header *header,
                            const U8 *grid_data, Usz grid_data_len) {
    Usz expected_size = (Usz)header->width * (Usz)header->height;
    if (grid_data_len < expected_size) {
        COLLAB_LOG_ERROR("Grid data too short: got %zu, expected %zu",
                         grid_data_len, expected_size);
        return false;
    }

    // Resize field if necessary
    field_resize_raw_if_necessary(field, header->height, header->width);

    // Copy grid data
    memcpy(field->buffer, grid_data, expected_size);

    COLLAB_LOG_DEBUG("Applied full sync: %ux%u grid, tick=%llu, playing=%d, bpm=%u",
                     header->width, header->height,
                     (unsigned long long)header->tick_num,
                     header->is_playing, header->bpm);

    return true;
}

// Serialize MSG_GRID_WRITE_BATCH
Usz collab_serialize_grid_write_batch(U8 *buf, U8 client_id,
                                       const Collab_write_tuple *writes,
                                       U16 num_writes) {
    buf[0] = client_id;
    collab_write_u16(buf + 1, num_writes);

    U8 *ptr = buf + MSG_GRID_WRITE_BATCH_HEADER_SIZE;
    for (U16 i = 0; i < num_writes; i++) {
        collab_write_u16(ptr + 0, writes[i].y);
        collab_write_u16(ptr + 2, writes[i].x);
        ptr[4] = (U8)writes[i].glyph;
        ptr += MSG_GRID_WRITE_TUPLE_SIZE;
    }

    return MSG_GRID_WRITE_BATCH_HEADER_SIZE + (Usz)num_writes * MSG_GRID_WRITE_TUPLE_SIZE;
}

// Deserialize MSG_GRID_WRITE_BATCH header
bool collab_deserialize_grid_write_batch_header(const U8 *buf, Usz len,
                                                 U8 *client_id, U16 *num_writes) {
    if (len < MSG_GRID_WRITE_BATCH_HEADER_SIZE) {
        return false;
    }

    *client_id = buf[0];
    *num_writes = collab_read_u16(buf + 1);

    // Validate that we have enough data for all writes
    Usz expected_size = MSG_GRID_WRITE_BATCH_HEADER_SIZE +
                        (Usz)(*num_writes) * MSG_GRID_WRITE_TUPLE_SIZE;
    if (len < expected_size) {
        return false;
    }

    // Sanity check on number of writes
    if (*num_writes > COLLAB_MAX_BATCH_WRITES) {
        return false;
    }

    return true;
}

// Parse a single write tuple from buffer
bool collab_parse_write_tuple(const U8 *buf, Collab_write_tuple *write) {
    write->y = collab_read_u16(buf + 0);
    write->x = collab_read_u16(buf + 2);
    write->glyph = (Glyph)buf[4];
    return true;
}

// Apply single write to field
bool collab_apply_write(Field *field, U16 y, U16 x, Glyph glyph) {
    if (!field || !field->buffer) {
        return false;
    }

    if (y >= field->height || x >= field->width) {
        COLLAB_LOG_WARN("Write out of bounds: (%u, %u) in %ux%u grid",
                        y, x, field->width, field->height);
        return false;
    }

    gbuffer_poke(field->buffer, field->height, field->width, y, x, glyph);
    return true;
}

// Apply batch of writes to field
Usz collab_apply_write_batch(Field *field, const Collab_write_tuple *writes,
                              Usz num_writes) {
    Usz applied = 0;
    for (Usz i = 0; i < num_writes; i++) {
        if (collab_apply_write(field, writes[i].y, writes[i].x, writes[i].glyph)) {
            applied++;
        }
    }
    return applied;
}

// Serialize MSG_RESYNC_REQUEST (empty payload)
Usz collab_serialize_resync_request(U8 *buf) {
    (void)buf;  // No payload
    return 0;
}

// Compare two grids for equality
bool collab_grids_equal(const Field *a, const Field *b) {
    if (!a || !b) {
        return a == b;
    }

    if (a->width != b->width || a->height != b->height) {
        return false;
    }

    if (!a->buffer && !b->buffer) {
        return true;
    }

    if (!a->buffer || !b->buffer) {
        return false;
    }

    Usz size = (Usz)a->width * (Usz)a->height;
    return memcmp(a->buffer, b->buffer, size) == 0;
}

// Copy grid from src to dest
void collab_copy_grid(const Field *src, Field *dest) {
    if (!src || !dest) {
        return;
    }

    field_resize_raw_if_necessary(dest, src->height, src->width);

    if (src->buffer && dest->buffer) {
        Usz size = (Usz)src->width * (Usz)src->height;
        memcpy(dest->buffer, src->buffer, size);
    }
}

// Fill grid with a character
void collab_fill_grid(Field *field, Glyph fill_char) {
    if (!field || !field->buffer) {
        return;
    }

    Usz size = (Usz)field->width * (Usz)field->height;
    memset(field->buffer, fill_char, size);
}
