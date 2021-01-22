#ifndef __FLEXALLOC_FREELIST_H_
#define __FLEXALLOC_FREELIST_H_

#include <stdint.h>
#include <stddef.h>

typedef uint32_t * freelist_t;

#define FLA_FREELIST_U32_ELEMS(b) FLA_CEIL_DIV(b, sizeof(uint32_t) * 8)

/**
 * Return size, in bytes, needed to support a freelist of `len` entries.
 *
 * @param len the number of entries to support.
 * @return the size required for the freelist, in bytes.
 */
size_t
fla_flist_size(uint32_t len);

/**
 * Return length of freelist.
 *
 * Returns the length of the freelist, defined as the number of slots
 * which can be reserved or taken.
 *
 * @param flist freelist handle
 * @return length of the freelist
 */
uint32_t
fla_flist_len(freelist_t flist);

/**
 * Return number of reserved entries in freelist.
 *
 * Return number of entries already reserved by the freelist.
 * Number of free entries can be determined by subtracting this value from
 * fla_flist_len().
 *
 * @param flist freelist handle
 * @return number of entries already reserved.
 */
uint32_t
fla_flist_num_reserved(freelist_t flist);

/**
 * Reset freelist, setting all `len` entries to free.
 *
 * Resets the freelist by freeing every one of the `len` entries.
 *
 * @param flist freelist handle
 */
void
fla_flist_reset(freelist_t flist);

/**
 * Initialize the freelist.
 *
 * Use this routine routine in case you allocate a buffer by other means
 * (use fla_flist_size() to determine the required size) and wish to initialize
 * the freelist.
 * Note: if you wish to re-use an existing freelist, calling fla_flist_reset()
 * will suffice.
 *
 * @param flist freelist handle
 * @param len length of the freelist. This should be the same length as provided
 * when using fla_flist_size() to calculate the required buffer size.
 */
void
fla_flist_init(freelist_t flist, uint32_t len);

/**
 * Create a new freelist.
 *
 * Allocates and initializes a new freelist.
 * Note that you can accomplish the same by using fla_flist_size() to calculate
 * the required buffer size for a list of `len` entries. Allocate the buffer
 * by whatever means you wish and call fla_flist_init() to initialize the
 * freelist before use.
 *
 * @param len length of the freelist to create
 * @param flist pointer to a freelist handle
 * @return On success, 0 is returned and flist points to an initialized freelist.
 * Otherwise, non-zero is returned and flist is uninitialized.
 */
int
fla_flist_new(uint32_t len, freelist_t *flist);

/**
 * Free buffer backing the freelist.
 *
 * NOTE: *only* use this if freelist is allocated using fla_flist_new().
 *
 * @param flist freelist handle
 */
void
fla_flist_free(freelist_t flist);

/**
 * Treat memory at data as an initialized freelist.
 *
 * Use data pointed at by `data` as a freelist. This expects the buffer to
 * be of sufficient length and its contents to be a freelist. That is, at
 * some point it was initialized with fla_freelist_init() and since then
 * only operated on using the fla_flist_* functions.
 *
 * NOTE: no additional allocations are made and that the memory is still
 * owned by you, do *not* use fla_flist_free() on the freelist handle!
 *
 * @param data a buffer with data laid out by the freelist routines.
 * @return a freelist handle
 * */
freelist_t
fla_flist_load(void *data);

/**
 * Allocate an entry from the freelist (if possible).
 *
 * Allocates an entry from the freelist, if possible, and returns its
 * index. An allocation may fail only if the freelist is full, in which
 * case -1 is returned.
 *
 * @param flist freelist handle
 * @return On success, a value between 0 and `len` (the freelist length)
 * indicating which element of the freelist has been reserved. On error,
 * -1, in which case no allocation was made.
 */
int
fla_flist_entry_alloc(freelist_t flist);

/**
 * Free an entry from the freelist.
 *
 * Frees the element identified by `ndx` in the freelist.
 *
 * NOTE: the free is idempotent, it is possible to free an already
 * freed element.
 *
 * NOTE: attempting to free an entry at an index outside the bounds
 * of the freelist range returns -1.
 *
 * @param flist freelist handle
 * @param ndx index within the freelist of the element to free
 * @return On success, 0 is returned. On error, -1 is returned.
 * */
int
fla_flist_entry_free(freelist_t flist, uint32_t ndx);

#endif // __FLEXALLOC_FREELIST_H_
