/**
 * @file aesd-circular-buffer.c
 * @brief Functions and data related to a circular buffer implementation
 *
 * @author Dan Walkes
 * @date 2020-03-01
 * @copyright Copyright (c) 2020
 *
 */

#ifdef __KERNEL__
#include <linux/string.h>
#else
#include <string.h>
#endif

#include "aesd-circular-buffer.h"

/**
 * Initializes the circular buffer described by @param buffer to an empty struct
 */
 
 
void aesd_circular_buffer_init(struct aesd_circular_buffer *buffer)
{
    memset(buffer, 0, sizeof(struct aesd_circular_buffer));
}

/**
 * Adds entry @param add_entry to @param buffer in the location specified in buffer->in_offs.
 * If the buffer was already full, overwrites the oldest entry and advances buffer->out_offs to the
 * new start location.
 * Any necessary locking must be handled by the caller.
 * Any memory referenced in @param add_entry must be allocated by and/or must have a lifetime managed by the caller.
 */
void aesd_circular_buffer_add_entry(struct aesd_circular_buffer *buffer, const struct aesd_buffer_entry *add_entry)
{
    // Overwrite the entry at in_offs
    buffer->entry[buffer->in_offs] = *add_entry;

    // If buffer is full, move out_offs to next oldest
    if (buffer->full) {
        buffer->out_offs = (buffer->out_offs + 1) % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;
    }

    // Advance in_offs
    buffer->in_offs = (buffer->in_offs + 1) % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;

    // If in_offs wrapped around to out_offs, buffer is now full
    buffer->full = (buffer->in_offs == buffer->out_offs);
}

/**
 * @param buffer the buffer to search for corresponding offset. Any necessary locking must be performed by caller.
 * @param char_offset the position to search for in the buffer list, describing the zero-referenced
 *      character index if all buffer strings were concatenated end to end
 * @param entry_offset_byte_rtn is a pointer specifying a location to store the byte of the returned aesd_buffer_entry
 *      buffptr member corresponding to char_offset. This value is only set when a matching char_offset is found
 *      in aesd_buffer.
 * @return the struct aesd_buffer_entry structure representing the position described by char_offset, or
 * NULL if this position is not available in the buffer (not enough data is written).
 */
struct aesd_buffer_entry *aesd_circular_buffer_find_entry_offset_for_fpos(
    struct aesd_circular_buffer *buffer,
    size_t char_offset,
    size_t *entry_offset_byte_rtn)
{
    if (!buffer || !entry_offset_byte_rtn) {
        return NULL;
    }

    size_t total_offset = 0;
    size_t entry_index;
    size_t valid_entries;

    // Determine number of valid entries in the buffer
    if (buffer->full) {
        valid_entries = AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;
    } else if (buffer->in_offs >= buffer->out_offs) {
        valid_entries = buffer->in_offs - buffer->out_offs;
    } else {
        valid_entries = AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED - buffer->out_offs + buffer->in_offs;
    }

    for (size_t i = 0; i < valid_entries; i++) {
        // Calculate the actual index in the buffer array
        entry_index = (buffer->out_offs + i) % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;

        size_t entry_size = buffer->entry[entry_index].size;

        if (char_offset < total_offset + entry_size) {
            *entry_offset_byte_rtn = char_offset - total_offset;
            return &buffer->entry[entry_index];
        }

        total_offset += entry_size;
    }

    // If we get here, the offset is beyond the valid buffer content
    return NULL;
}

