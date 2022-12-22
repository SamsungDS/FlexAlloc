/**
 * flexalloc hash functions and hash table implementation
 *
 * Copyright (C) 2021 Jesper Devantier <j.devantier@samsung.com>
 *
 * @file flexalloc_hash.h
 */
#ifndef __FLEXALLOC_HASH_H_
#define __FLEXALLOC_HASH_H_
#include <stdint.h>

#define FLA_HTBL_ENTRY_UNSET ~0
#define FLA_HTBL_H1(key) fla_hash_djb2(key)
#define FLA_HTBL_H2(key) fla_hash_sdbm(key)
#define FLA_HTBL_COMPRESS(hval, tbl_size) fla_mad_compression(hval, 31, 5745, tbl_size)

/**
 * DJB2 hash function.
 *
 * @param key text value to hash
 * @return hash value of key
 */
uint64_t
fla_hash_djb2(const char *key);

/**
 * SDBM hash function.
 *
 * @param key text value to hash
 * @return hash value of key
 */
uint64_t
fla_hash_sdbm(char const *key);

/**
 * (M)ultiply (a)dd (d)ivide compression algorithm.
 *
 * The compression function is used to compress hash values (2^64
 * range of values) to some smaller range, [0;n[.
 *
 * Note: it should hold that: a % n != 0
 *
 * @param key hash some hash value
 * @param a some non-negative integer
 * @param b some non-negative integer
 * @param n maximum permitted value (your hash table's size)
 *
 * @return some value X where 0 <= x < n
 */
uint64_t
fla_mad_compression(uint64_t hash, uint64_t a, uint64_t b, uint64_t n);

/// Hash table data structure
struct fla_htbl
{
  /// The underlying array of table entries
  struct fla_htbl_entry *tbl;
  /// Hash table size
  unsigned int tbl_size;
  /// Number of items presently in hash table
  unsigned int len;

  unsigned int stat_insert_calls;
  unsigned int stat_insert_failed;
  unsigned int stat_insert_tries;
};

/// Hash table entry
struct fla_htbl_entry
{
  /// Secondary hash value
  ///
  /// The secondary hash value is used to distinguish collisions where two
  /// distinct input values yield the same hash value when using our primary
  /// hash function.
  /// Inserting a secondary hash into the table makes genuine collisions
  /// highly improbable.
  uint64_t h2;
  /// The hash table entry value
  ///
  /// In case of
  uint32_t val;
  /// Probe sequence length value
  ///
  /// The probe sequence length value effectively tracks how many
  /// places the element is away from its ideal hash table position.
  ///
  /// We use this value with Robin Hood hashing during insertion to give
  /// priority to elements with a higher PSL - inserting our element if it
  /// has a higher PSL (already father from its ideal entry) than the currently
  /// examined element, continuing insertion by then trying to find a spot for
  /// the displaced element.
  uint16_t psl;
};

/**
 * Allocates (& initializes) a new hash table with `tbl_size` entries.
 *
 * @param tbl_size desired size of the backing table
 *        NOTE: the table size should be 2-3 times the size of the
 *        expected number of elements. Hash tables perform perform
 *        very poorly as they fill up.
 * @param htbl pointer to hash table, will contain a reference to
 *        the constructed hash table
 *
 * @return On success 0 and *htbl pointing to an allocated and initialized
 * hash table. On error, non-zero and *htbl being undefined.
 */
int htbl_new(unsigned int tbl_size, struct fla_htbl **htbl);

void fla_htbl_entries_init(struct fla_htbl_entry *tbl, uint32_t tbl_size);

/**
 * Initialize hash table.
 *
 * NOTE: automatically called by htbl_new
 */
int htbl_init(struct fla_htbl *htbl, unsigned int tbl_size);

/**
 * Free hash table.
 *
 * @param htbl hash table
 */
void htbl_free(struct fla_htbl *htbl);

/**
 * Insert entry into hash table.
 *
 * NOTE: htbl_insert will update the existing entry if present.
 *
 * @return 0 if entry inserted or updated, otherwise an error.
 */
int htbl_insert(struct fla_htbl *htbl, char const *key, uint32_t val);

/**
 * Find entry in hash table.
 *
 * @param htbl hash table
 * @param key key of entry to find
 *
 * @return NULL if no entry was found, otherwise the entry
 */
struct fla_htbl_entry *htbl_lookup(struct fla_htbl *htbl, const char *key);

/**
 * Remove entry from hash table.
 *
 * @param htbl the hash table
 * @param key key of the entry to remove
 */
void htbl_remove_key(struct fla_htbl *htbl, char const *key);

void htbl_remove_entry(struct fla_htbl *htbl, struct fla_htbl_entry *entry);

#endif // __FLEXALLOC_HASH_H_
