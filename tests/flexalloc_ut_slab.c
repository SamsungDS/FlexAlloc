// Copyright (C) 2021 Joel Granados <j.granados@samsung.com>
#include "tests/flexalloc_tests_common.h"
#include "src/flexalloc_util.h"
#include "src/flexalloc_mm.h"
#include "src/flexalloc_ll.h"
#include <stdint.h>

struct test_vals
{
  uint64_t blk_num;
  uint32_t npools;
  uint32_t slab_nlb;
  uint32_t nslabs;
  uint32_t obj_nlb;
};

static int test_slabs(const struct test_vals * test_vals);
static int test_check_slab_pointers(struct flexalloc * fs, const uint32_t expected_size);

#define FLA_UT_SLAB_NUMBER_OF_TESTS 4
int
main(int argc, char ** argv)
{
  int err = 0;
  struct test_vals test_vals [FLA_UT_SLAB_NUMBER_OF_TESTS] =
  {
    {.blk_num = 10, .npools = 2, .slab_nlb = 2, .nslabs = 2, .obj_nlb = 1 }
    , {.blk_num = 20, .npools = 2, .slab_nlb = 2, .nslabs = 7, .obj_nlb = 1 }
    , {.blk_num = 50, .npools = 2, .slab_nlb = 20, .nslabs = 2, .obj_nlb = 2 }
    , {.blk_num = 20, .npools = 2, .slab_nlb = 5, .nslabs = 3, .obj_nlb = 1 }
  };

  for(int i = 0 ; i < FLA_UT_SLAB_NUMBER_OF_TESTS ; ++i)
  {
    err = test_slabs(&test_vals[i]);
    if(FLA_ERR(err, "test_slabs()"))
    {
      goto exit;
    };
  }

exit:
  return err;
}

int
test_slabs(const struct test_vals * test_vals)
{
  int err = 0, ret;
  struct fla_ut_dev dev;
  struct flexalloc *fs;
  struct fla_slab_header *slab_header, *slab_error;

  slab_error = malloc(sizeof(struct fla_slab_header));
  if (FLA_ERR(!slab_error, "malloc()"))
  {
    err = -ENOMEM;
    goto exit;
  }
  err = fla_ut_fs_create(test_vals->blk_num, test_vals->slab_nlb, test_vals->npools, &dev, &fs);
  if (FLA_ERR(err, "fla_ut_fs_create()"))
  {
    goto free_slab_error;
  }

  /* Test values in struct fla_slabs */
  err |= FLA_ASSERTF(*fs->slabs.fslab_num == test_vals->nslabs,
                     "Unexpected number of free slabs (%d == %d)", *fs->slabs.fslab_num, test_vals->nslabs);
  err |= FLA_ASSERTF(*fs->slabs.fslab_head == 0,
                     "Unexpected head ID (%d == %d)", *fs->slabs.fslab_head, 0);
  err |= FLA_ASSERTF(*fs->slabs.fslab_tail == test_vals->nslabs - 1,
                     "Unexpected tail ID (%d == %d)", *fs->slabs.fslab_tail, test_vals->nslabs - 1);

  /* Acquire all the slabs and then release them all */
  for(uint32_t slab_offset = 0 ; slab_offset < test_vals->nslabs ; ++slab_offset)
  {
    slab_header = (void*)fs->slabs.headers + (slab_offset * sizeof(struct fla_slab_header));

    err = fla_acquire_slab(fs, test_vals->obj_nlb, &slab_header);
    if(FLA_ERR(err, "fla_acquire_slab()"))
    {
      goto close_fs;
    }

    err = FLA_ASSERT(slab_header->next == FLA_LINKED_LIST_NULL,
                     "Next pointer is not null after slab format");
    err |= FLA_ASSERT(slab_header->prev == FLA_LINKED_LIST_NULL,
                      "Prev pointer is not null after slab format");
    if(FLA_ERR(err, "FLA_ASSERT()"))
    {
      goto close_fs;
    }

    const uint32_t free_slabs = test_vals->nslabs - (slab_offset + 1);
    err = FLA_ASSERTF(*fs->slabs.fslab_num == free_slabs,
                      "Unexpected number of free slabs (%d == %d)", *fs->slabs.fslab_num, free_slabs);
    if(FLA_ERR(err, "FLA_ASSERTF()"))
    {
      goto close_fs;
    }

    err = test_check_slab_pointers(fs, free_slabs);
    if(FLA_ERR(err, "test_check_slab_pointers()"))
    {
      goto close_fs;
    }
  }

  /* If we acquire another slab, we should receive an error */
  ret = fla_acquire_slab(fs, test_vals->obj_nlb, &slab_error);
  err = FLA_ASSERT(ret != 0, "Acquire of an empty free list did NOT fail");
  if(FLA_ERR(err, "FLA_ASSERT()"))
  {
    goto close_fs;
  }

  for(uint32_t slab_offset = 0 ; slab_offset < test_vals->nslabs ; ++slab_offset)
  {
    slab_header = (void*)fs->slabs.headers + (slab_offset * sizeof(struct fla_slab_header));

    err = fla_release_slab(fs, slab_header);
    if(FLA_ERR(err, "fla_release_slab()"))
    {
      goto close_fs;
    }

    err = FLA_ASSERTF(*fs->slabs.fslab_num == slab_offset + 1,
                      "Unexpected number of free slabs (%d == %d)", *fs->slabs.fslab_num, slab_offset + 1);
    if(FLA_ERR(err, "FLA_ASSERTF()"))
    {
      goto close_fs;
    }
  }

close_fs:
  ret = fla_ut_fs_teardown(&dev, fs);
  if (FLA_ERR(ret, "fla_ut_fs_teardown()"))
  {
    err = ret;
  }

free_slab_error:
  free(slab_error);

exit:
  return err;

}

int
test_check_slab_pointers(struct flexalloc * fs, const uint32_t expected_size)
{
  int err = 0;
  struct fla_slab_header * curr_slab;
  uint32_t curr_slab_id, size_from_head = 0;

  /* check next pointers */
  curr_slab_id = *fs->slabs.fslab_head;
  for (uint32_t i = 0 ; i <= expected_size && curr_slab_id != INT32_MAX; ++i)
  {
    curr_slab = fla_slab_header_ptr(curr_slab_id, fs);
    if((err = -FLA_ERR(!curr_slab, "fla_slab_header_ptr()")))
    {
      goto exit;
    }
    curr_slab_id = curr_slab->next;
    size_from_head++;
  }

  err = FLA_ASSERTF(size_from_head == expected_size,
                    "Unexpected size when starting from head (%d == %d)", size_from_head, expected_size);

  /* check prev pointers */
  curr_slab_id = *fs->slabs.fslab_tail;
  for (uint32_t i = 0; i <= expected_size && curr_slab_id != INT32_MAX; ++i)
  {
    curr_slab = fla_slab_header_ptr(curr_slab_id, fs);
    if((err = -FLA_ERR(!curr_slab, "fla_slab_header_ptr()")))
    {
      goto exit;
    }
    curr_slab_id = curr_slab->prev;
    size_from_head++;

  }

exit:
  return err;
}
