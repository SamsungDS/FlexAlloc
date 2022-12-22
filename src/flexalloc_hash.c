#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "flexalloc_util.h"
#include "flexalloc_hash.h"

uint64_t
fla_hash_djb2(const char *key)
{
  uint64_t hash = 5381;
  unsigned int key_len = strlen(key);

  for (unsigned int i = 0; i < key_len; i++)
  {
    hash = ((hash << 5) + hash) + key[i]; // hash * 33 + c
  }

  return hash;
}

uint64_t
fla_hash_sdbm(char const *key)
{
  uint64_t hash = 0;
  int c;

  while ((c = *key++))
    hash = c + (hash << 6) + (hash << 16) - hash;

  return hash;
}

uint64_t
fla_mad_compression(uint64_t key, uint64_t a, uint64_t b, uint64_t n)
{
  // n should be the table size (and ideally a prime number)
  // a, b should be non-negative integers
  // a % n != 0

  // map `key` to some value within [0;N[
  // (abs is implied as all values are unsigned)
  return (a * key + b) % n;
}

void
fla_htbl_entries_init(struct fla_htbl_entry *tbl, uint32_t tbl_size)
{
  memset(tbl, 0, sizeof(struct fla_htbl_entry) * tbl_size);

  for (unsigned int i = 0; i < tbl_size; i++)
  {
    // indicate that slot is unset
    tbl[i].h2 = FLA_HTBL_ENTRY_UNSET;
    tbl[i].psl = 0;
  }
}

int
htbl_init(struct fla_htbl *htbl, unsigned int tbl_size)
{
  struct fla_htbl_entry *table;
  int err = 0;

  if ((err = FLA_ERR(htbl->tbl != NULL, "cannot initialize twice")))
    goto exit;

  table = calloc(sizeof(struct fla_htbl_entry), tbl_size);
  if ((err = FLA_ERR(!table, "failed to allocate table for entries")))
  {
    err = -ENOMEM;
    goto exit;
  }

  htbl->tbl = table;
  htbl->tbl_size = tbl_size;

  fla_htbl_entries_init(htbl->tbl, htbl->tbl_size);

  htbl->len = 0;

  htbl->stat_insert_calls = 0;
  htbl->stat_insert_failed = 0;
  htbl->stat_insert_tries = 0;

exit:
  return err;
}

int
htbl_new(unsigned int tbl_size, struct fla_htbl **htbl)
{
  int err = 0;
  *htbl = malloc(sizeof(struct fla_htbl));
  if (FLA_ERR(!(*htbl), "failed to allocate hash table"))
  {
    err = -ENOMEM;
    return err;
  }
  (*htbl)->tbl = NULL;

  err = htbl_init(*htbl, tbl_size);
  if (FLA_ERR(err, "htbl_init()"))
    goto free_table;

  return 0;

free_table:
  free(*htbl);
  return err;
}

void
htbl_free(struct fla_htbl *htbl)
{
  if (!htbl)
    return;
  free(htbl->tbl);
  free(htbl);
}

int
htbl_insert(struct fla_htbl *htbl, char const *key, uint32_t val)
{
  unsigned int n_tries = 0;
  uint64_t ndx = FLA_HTBL_COMPRESS(FLA_HTBL_H1(key), htbl->tbl_size);

  struct fla_htbl_entry *entry;
  struct fla_htbl_entry tmp, current;
  current.h2 = FLA_HTBL_H2(key);
  current.psl = 0;
  current.val = val;

  htbl->stat_insert_calls++;

  if (htbl->len == htbl->tbl_size)
    // table is full
    return 2;

  while (1)
  {
    entry = &htbl->tbl[ndx];
    n_tries++;

    if (entry->h2 == FLA_HTBL_ENTRY_UNSET)
    {
      // found empty slot, insert and quit
      *entry = current;
      htbl->len++;
      htbl->stat_insert_tries += n_tries;
      return 0;
    }
    else if (entry->psl < current.psl)
    {
      // richer element, swap out and continue insert
      tmp = *entry;

      *entry = current;

      current = tmp;
      current.psl += 1;
    }
    else if (entry->h2 == current.h2)
    {
      // entry with same key (or *very* unlikely collision)
      entry->val = current.val;
      return 0;
    }
    else
    {
      // continue search (inc psl and ndx)
      current.psl++;
    }
    ndx++;
    if (ndx == htbl->tbl_size)
    {
      ndx = 0;
    }
  }
}

// lookup - based on the robinhood placement strategy
struct fla_htbl_entry *
__htbl_lookup(struct fla_htbl *htbl, uint64_t h2, uint64_t ndx)
{
  unsigned int psl = 0;
  struct fla_htbl_entry *entry;

  while(1)
  {
    entry = &htbl->tbl[ndx++];
    if (entry->h2 == h2)
      return entry;
    else if (entry->h2 == FLA_HTBL_ENTRY_UNSET || entry->psl < psl)
      // empty entry OR an entry whose placement would make it richer than the one
      // we're looking for, which would be impossible given our placement strategy.
      break;

    if (ndx == htbl->tbl_size)
      // wrap around
      ndx = 0;
    psl++;
  }
  return NULL;
}

struct fla_htbl_entry *
htbl_lookup(struct fla_htbl *htbl, char const *key)
{
  return __htbl_lookup(htbl, FLA_HTBL_H2(key), FLA_HTBL_COMPRESS(FLA_HTBL_H1(key),
                       htbl->tbl_size));
}

void
htbl_remove_entry(struct fla_htbl *htbl, struct fla_htbl_entry *entry)
{
  struct fla_htbl_entry *next;
  struct fla_htbl_entry *end = htbl->tbl + htbl->tbl_size;

  if (!entry)
    return;

  while(1)
  {
    next = entry + 1;
    if (next == end)
      next = htbl->tbl;

    if (!next->psl)
      break;

    *entry = *next;
    entry->psl--;

    entry = next;
  }

  entry->h2 = FLA_HTBL_ENTRY_UNSET;
  entry->psl = 0;
  htbl->len--;
}

// remove - based on the robinhood placement strategy
void
htbl_remove_key(struct fla_htbl *htbl, char const *key)
{
  uint64_t h2 = FLA_HTBL_H2(key);
  uint64_t ndx = FLA_HTBL_COMPRESS(FLA_HTBL_H1(key), htbl->tbl_size);
  struct fla_htbl_entry *entry = __htbl_lookup(htbl, h2, ndx);
  return htbl_remove_entry(htbl, entry);
}
