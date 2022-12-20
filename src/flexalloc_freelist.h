#ifndef __FLEXALLOC_FREELIST_H_
#define __FLEXALLOC_FREELIST_H_

#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>

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
 * @param unsigned int num of entries to allocate
 * @return On success, a value between 0 and `len` (the freelist length)
 * indicating which element of the freelist has been reserved. On error,
 * -1, in which case no allocation was made.
 */
int
fla_flist_entries_alloc(freelist_t flist, unsigned int num);

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


int
fla_flist_entries_free(freelist_t flist, uint32_t ndx, unsigned int num);

/**
 * Search all the used element by executing a function.
 *
 *
 * @param flist freelist handle
 * @param flags modifies how the function is executed.
 *        When FLA_FLIST_SEARCH_EXEC_FIRST is set returns on first find
 * @param found is the number of times f returned 1
 * @param f The function that will be executed. Must Return < 0
 *        on error, must return 0 when an element was not "found",
 *        must return 1 when an element was "found".
 * @param ... These are the variadic arguments that will be
 *        forwarded to the f function.
 * @return <0 if there is an error. 0 otherwise.
 */
int
fla_flist_search_wfunc(freelist_t flist, uint64_t flags, uint32_t *found,
                       int(*f)(const uint32_t, va_list), ...);
#define FLA_FLIST_SEARCH_FROM_START 1 << 0
enum {
  FLA_FLIST_SEARCH_RET_FOUND_STOP,
  FLA_FLIST_SEARCH_RET_FOUND_CONTINUE,
  FLA_FLIST_SEARCH_RET_STOP,
  FLA_FLIST_SEARCH_RET_CONTINUE,
  FLA_FLIST_SEARCH_RET_ERR,
};

#endif // __FLEXALLOC_FREELIST_H_
