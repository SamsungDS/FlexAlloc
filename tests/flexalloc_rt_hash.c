#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <string.h>
#include "flexalloc_hash.h"
#include "flexalloc_util.h"

char default_file[] = "wlist.txt";

struct linereader
{
  FILE *fp;
  char *buf;
  unsigned int buf_len;
};

int
linereader_init(struct linereader *lr, char *fname, char *buf, unsigned int buf_len)
{
  FILE *fp = fopen(fname, "r");
  int err = 0;

  if ((err = FLA_ERR(!fp, "fopen() failed to open file")))
  {
    err = -1;
    goto exit;
  }

  lr->fp = fp;
  lr->buf = buf;
  lr->buf_len = buf_len;

exit:
  return err;
}

void
linereader_destroy(struct linereader *lr)
{
  if (!lr)
    return;

  fclose(lr->fp);
}

void
linereader_seek_to_start(struct linereader *lr)
{
  fseek(lr->fp, 0, SEEK_SET);
}

int
linereader_next(struct linereader *lr)
{
  unsigned int i = 0;

  for (; i < lr->buf_len; i++)
  {
    lr->buf[i] = fgetc(lr->fp);

    if (lr->buf[i] == '\n')
    {
      lr->buf[i] = '\0';
      break;
    }
    else if (lr->buf[i] == EOF)
    {
      lr->buf[i] = '\0';
      if (i == 0)
      {
        // indicate that nothing was read
        return -1;
      }
      break;
    }
  }
  return 0;
}

// 0 > RET, failed some other step
// 0 == success
// N, where N > 0, failed to insert N entries
int
test_insert(struct fla_htbl *htbl, struct linereader *lr)
{
  // ensure test files have no duplicate strings
  // lay out table with all entries to unset
  unsigned int lineno = 0;
  int err = 0;

  linereader_seek_to_start(lr);

  while (linereader_next(lr) >= 0)
  {
    if (htbl_insert(htbl, lr->buf, lineno++))
    {
      err++;
    }
  }
  if(err)
  {
    fprintf(stdout, "========================================\n");
    fprintf(stdout, "test_insert\n");
    fprintf(stdout, "========================================\n");
  }

  return err;
}

// 0 > RET if initialization failed
// 0 == success
// N, where N > 0 -- had N failed lookups
int
test_lookup(struct fla_htbl *htbl, struct linereader *reader)
{
  // ensure test files have no duplicate strings
  // lay out table with all entries to unset
  struct fla_htbl_entry *entry = NULL;
  int err = 0;

  linereader_seek_to_start(reader);

  while (linereader_next(reader) >= 0)
  {
    entry = htbl_lookup(htbl, reader->buf);
    if (!entry)
    {
      err++;
    }
    else if (entry->h2 != FLA_HTBL_H2(reader->buf))
    {
      err++;
    }
  }

  if(err)
  {
    fprintf(stdout, "========================================\n");
    fprintf(stdout, "test_lookup\n");
    fprintf(stdout, "========================================\n");
  }

  return err;
}

// 0 > RET if initialization failed
// 0 == success
// N, where N > 0 -- had N instances of failing to remove entry
int
test_remove(struct fla_htbl *htbl, struct linereader *reader)
{
  // ensure test files have no duplicate strings
  // lay out table with all entries to unset
  struct fla_htbl_entry *entry = NULL;
  int err = 0;

  linereader_seek_to_start(reader);

  while (linereader_next(reader) >= 0)
  {
    htbl_remove(htbl, reader->buf);

    entry = htbl_lookup(htbl, reader->buf);
    if (entry)
      err++;
  }
  if(err)
  {
    fprintf(stdout, "========================================\n");
    fprintf(stdout, "test_remove\n");
    fprintf(stdout, "========================================\n");
  }

  return err;
}

void
htbl_stats_print(struct fla_htbl *htbl)
{
  unsigned int entries = 0;
  unsigned int psl_max = 0;
  for (unsigned int i = 0; i < htbl->tbl_size; i++)
  {
    if (htbl->tbl[i].h2 != FLA_HTBL_ENTRY_UNSET)
    {
      entries++;
      if (htbl->tbl[i].psl > psl_max)
        psl_max = htbl->tbl[i].psl;
    }
  }

  FLA_VBS_PRINTF("psl max: %d\n", psl_max);
  FLA_VBS_PRINTF("lines: %d\n", htbl->stat_insert_calls);
  FLA_VBS_PRINTF("entries: %d\n", entries);
  FLA_VBS_PRINTF("table size: %d\n", htbl->tbl_size);
  FLA_VBS_PRINTF("avg placement tries: %f\n",
                 (double)htbl->stat_insert_tries / (double)htbl->stat_insert_calls);
  FLA_VBS_PRINTF("failed placements: %d\n", htbl->stat_insert_failed);
}

int
main(int argc, char **argv)
{
  char *fname;
  struct fla_htbl *htbl = NULL;
  unsigned int tbl_size = 83922 * 2;
  struct linereader reader;
  char linebuf[150];
  int err = 0;
  if (argc > 2)
  {
    fprintf(stderr, "usage: $0 [<test file>]\n");
    err = -1;
    goto exit;
  }
  else if (argc == 1)
  {
    fname = default_file;
  }
  else
  {
    fname = argv[1];
  }
  err = linereader_init(&reader, fname, linebuf, 150);
  if (FLA_ERR(err, "failed to initialize line reader"))
  {
    goto exit;
  }

  err = htbl_new(tbl_size, &htbl);
  if (FLA_ERR(err, "failed to allocate table"))
  {
    err = -1;
    goto reader_destroy;
  }

  err = test_insert(htbl, &reader);
  if (FLA_ERR(err, "failed during insert"))
  {
    fprintf(stderr, "failed %d inserts\n", err);
  }
  htbl_stats_print(htbl);

  err = test_lookup(htbl, &reader);
  if (FLA_ERR(err, "failed during lookup"))
  {
    fprintf(stdout, "failed %d lookups\n", err);
    goto exit;
  }

  err = test_remove(htbl, &reader);
  if (FLA_ERR(err, "failed to remove entries"))
  {
    goto table_free;
  }

  htbl_stats_print(htbl);

table_free:
  htbl_free(htbl);

reader_destroy:
  linereader_destroy(&reader);

exit:
  return err;
}
