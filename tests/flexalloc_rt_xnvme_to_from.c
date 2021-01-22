//Copyright (C) 2021 Joel Granados <j.granados@samsung.com>
#define FLA_TESTING
#include <stdint.h>
#include "flexalloc_tests_common.h"
#include "src/flexalloc_util.h"
#include "src/flexalloc_xnvme_env.h"
#include <unistd.h>
#include <libxnvmec.h>

int test_to_stg(const int buf_size, const int blk_num, const int blk_size,
                struct fla_ut_lpbk * lpbk, struct xnvme_dev * xnvme_dev, char * buf);

int test_from_stg(const int buf_size, const int blk_num, const int blk_size,
                  struct fla_ut_lpbk * lpbk, struct xnvme_dev * xnvme_dev, char * buf);

int
main(int argc, char ** argv)
{
  int ret = 0, err = 0, blk_size = 512, blk_num = 3,
      buf_size = blk_size*blk_num;
  struct fla_ut_lpbk * lpbk;
  struct xnvme_dev * xnvme_dev;
  char * buf;

  ret = fla_ut_lpbk_dev_alloc(blk_size, blk_num, &lpbk);
  if(FLA_ERR(ret, "fla_ut_lpbk_dev_alloc()"))
  {
    goto exit;
  }

  ret = fla_xne_dev_open(lpbk->dev_name, NULL, &xnvme_dev);
  if((ret = FLA_ERR(ret, "fla_xne_dev_open()")))
  {
    xnvmec_perr("xnvme_dev_open()", errno);
    ret = -EIO;
    goto loop_free;
  }

  buf =  fla_xne_alloc_buf(xnvme_dev, buf_size);
  if((ret = FLA_ERR(!buf, "fla_buf_alloc()")))
  {
    goto close_dev;
  }

  ret = test_to_stg(buf_size, blk_num, blk_size, lpbk, xnvme_dev, buf);
  if(FLA_ERR(ret, "test_to_stg()"))
  {
    goto free_buf;
  }

  ret = test_from_stg(buf_size, blk_num, blk_size, lpbk, xnvme_dev, buf);
  if(FLA_ERR(ret, "test_from_stg()"))
  {
    goto free_buf;
  }

free_buf:
  fla_xne_free_buf(xnvme_dev, buf);

close_dev:
  xnvme_dev_close(xnvme_dev);

loop_free:
  err = fla_ut_lpbk_dev_free(lpbk);
  if(FLA_ERR(err, "fla_ut_lpbk_dev_free()") && !ret)
  {
    ret = err;
  }

exit:
  return ret != 0;
}

int
test_from_stg(const int buf_size, const int blk_num, const int blk_size,
              struct fla_ut_lpbk * lpbk, struct xnvme_dev * xnvme_dev, char * buf)
{
  int ret = 0, num_ones;

  ret = fla_ut_lpbk_overwrite('1', lpbk);
  if(FLA_ERR(ret, "fla_ut_lpbk_overwrite()"))
  {
    goto exit;
  }

  for(int slba = 0; slba < blk_num; ++slba)
  {
    for(int elba = slba; elba < blk_num; ++ elba)
    {
      memset(buf, '0', buf_size);

      // -1 because naddrs is a non zero value.
      ret = fla_xne_sync_seq_r_naddrs(xnvme_dev, slba, elba - slba + 1, buf);
      if(FLA_ERR(ret, "fla_xne_sync_seq_w()"))
        goto exit;

      num_ones = fla_ut_count_char_in_buf('1', buf, buf_size);

      ret = FLA_ASSERTF(num_ones == (elba - slba + 1) * blk_size,
                        "Read unexpected number of bytes. slba : %d, elba : %d, num_ones : %d",
                        slba, elba, num_ones);
      if(FLA_ERR(ret, "FLA_ASSERT()"))
        goto exit;
    }
  }

exit:
  return ret;
}

int
test_to_stg(const int buf_size, const int blk_num, const int blk_size,
            struct fla_ut_lpbk * lpbk, struct xnvme_dev * xnvme_dev, char * buf)
{
  int ret = 0, num_ones;
  memset(buf, '1', buf_size);

  for(int slba = 0; slba < blk_num; ++slba)
  {
    for(int elba = slba; elba < blk_num; ++ elba)
    {
      ret = fla_ut_lpbk_overwrite('0', lpbk);
      if(FLA_ERR(ret, "fla_ut_lpbk_overwrite()"))
        goto exit;

      // -1 because naddrs is a non zero value.
      ret = fla_xne_sync_seq_w_naddrs(xnvme_dev, slba, elba - slba + 1, buf);
      if(FLA_ERR(ret, "fla_xne_sync_seq_w_naddrs()"))
        goto exit;

      ret = fsync(lpbk->bfile_fd);
      if((ret = FLA_ERR_ERRNO(ret, "fsync()")))
        goto exit;

      ret = fla_ut_count_char_in_file('1', lpbk->bfile_fd, 0, blk_size*blk_num, &num_ones);
      if(FLA_ERR(ret, "fla_ut_count_char_in_file()"))
        goto exit;

      ret = FLA_ASSERTF(num_ones == (elba - slba + 1) * blk_size,
                        "Wrote unexpected number of bytes. slba : %d, elba : %d, num_ones : %d",
                        slba, elba, num_ones);
      if(FLA_ERR(ret, "FLA_ASSERT()"))
        goto exit;
    }
  }

exit:
  return ret;
}

