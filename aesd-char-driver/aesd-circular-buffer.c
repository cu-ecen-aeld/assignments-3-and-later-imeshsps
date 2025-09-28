/**
 * @file aesd-circular-buffer.c
 * @brief Circular buffer implementation with memory management for AESD char driver
 */

#ifdef __KERNEL__
#include <linux/string.h>
#include <linux/slab.h>
#else
#include <string.h>
#include <stdlib.h>
#endif

#include "aesd-circular-buffer.h"

/**
 * Initializes the circular buffer to an empty state
 */
void aesd_circular_buffer_init(struct aesd_circular_buffer *buffer)
{
    size_t i;
    if (!buffer)
        return;

    for (i = 0; i < AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED; i++) {
        buffer->entry[i].buffptr = NULL;
        buffer->entry[i].size = 0;
    }
    buffer->in_offs = 0;
    buffer->out_offs = 0;
    buffer->full = 0; /* false */
}

/**
 * Adds entry to the circular buffer
 * Frees memory for overwritten entries if necessary
 */
void aesd_circular_buffer_add_entry(struct aesd_circular_buffer *buffer,
                                    const struct aesd_buffer_entry *add_entry)
{
    if (!buffer || !add_entry)
        return;

    /* Free old entry if it exists */
    if (buffer->entry[buffer->in_offs].buffptr != NULL) {
#ifdef __KERNEL__
        kfree((void *)buffer->entry[buffer->in_offs].buffptr);
#else
        free((void *)buffer->entry[buffer->in_offs].buffptr);
#endif
    }

    /* Copy the new entry (pointer ownership moves to buffer) */
    buffer->entry[buffer->in_offs] = *add_entry;

    /* If buffer is full, advance out_offs */
    if (buffer->full) {
        buffer->out_offs = (buffer->out_offs + 1) % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;
    }

    /* Advance in_offs */
    buffer->in_offs = (buffer->in_offs + 1) % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;

    /* Check if buffer is full */
    if (buffer->in_offs == buffer->out_offs) {
        buffer->full = 1; /* true */
    } else {
        buffer->full = 0;
    }
}

/**
 * Find buffer entry corresponding to a file offset
 */
struct aesd_buffer_entry *aesd_circular_buffer_find_entry_offset_for_fpos(
    struct aesd_circular_buffer *buffer,
    size_t char_offset,
    size_t *entry_offset_byte_rtn)
{
    size_t total_offset;
    size_t entry_index;
    size_t valid_entries;
    size_t i;
    size_t entry_size;

    if (!buffer || !entry_offset_byte_rtn)
        return NULL;

    total_offset = 0;

    if (buffer->full) {
        valid_entries = AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;
    } else if (buffer->in_offs >= buffer->out_offs) {
        valid_entries = buffer->in_offs - buffer->out_offs;
    } else {
        valid_entries = AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED - buffer->out_offs + buffer->in_offs;
    }

    for (i = 0; i < valid_entries; i++) {
        entry_index = (buffer->out_offs + i) % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;
        entry_size = buffer->entry[entry_index].size;

        if (char_offset < total_offset + entry_size) {
            *entry_offset_byte_rtn = char_offset - total_offset;
            return &buffer->entry[entry_index];
        }
        total_offset += entry_size;
    }

    return NULL;
}

