// Copyright (C) 2021 Joel Granados <j.granados@samsung.com>
// Copyright (C) 2021 Jesper Devantier <j.devantier@samsung.com>

#include <asm-generic/errno-base.h>
#define FUSE_USE_VERSION 31
#include <stdio.h>
#include <stddef.h>
#include <getopt.h>
#include <string.h>
#include <stdlib.h>
#include <fuse.h>


struct fla_fuse_opts
{
  char *dev;
};

static struct option long_opts[] =
{
  {"dev", required_argument, NULL, 'd'},
  {NULL, 0, NULL, 0},
};

void
fla_fuse_usage()
{
  fprintf(stderr, "Usage: fuse.flexalloc -d <device>\n");
}

/* calculate string length
 *
 * returns:
 * - 0 if `src` is NULL
 * - number of characters until first null byte OR `strsz` if
 *   string is as long or longer than `strsz`.
 */
size_t
fla_strnlen(char const *src, size_t strsz)
{
  char *p = (char *)src;
  char const *p_end = src + strsz;
  if (!src)
    return 0;

  while (p != p_end)
  {
    if (*p == '\0')
      break;
    p++;
  }
  return p - src;
}

/* duplicate string
 *
 * duplicates at most `strsz` first bytes of string `src`.
 *
 * returns:
 * - NULL ptr if string duplication failed
 * - pointer to newly allocated string
 */
char *
fla_strndup(char const *src, size_t maxlen)
{
  if (!src)
  {
    return NULL;
  }
  size_t len = fla_strnlen(src, maxlen);
  char *dst = malloc(len + 1);
  if (!dst)
  {
    return NULL;
  }

  memcpy(dst, src, len);

  dst[len] = '\0';
  return dst;
}

int
fla_fuse_parse_args(struct fla_fuse_opts *opts, int argc, char **argv)
{
  int ch;
  while ((ch = getopt_long(argc, argv, "d:", long_opts, NULL)) != -1)
  {
    switch (ch)
    {
    case 'd':
      fprintf(stdout, "got -d '%s'\n", optarg);
      opts->dev = fla_strndup(optarg, 5);
      fprintf(stdout, "copied over: '%s'\n", opts->dev);
      break;
    default:
      // getopt will already print an error showing the unrecognized option
      fla_fuse_usage();
      return -1;
    }
  }
  return 0;
}

int
fla_fuse_cb_getattr(const char *path, struct stat *st_data,
                    struct fuse_file_info *fi)
{
  fprintf(stderr, "getattr - unsupported OP\n");
  return -1;
}

static int
fla_fuse_cb_readlink(const char *path, char *buf, size_t size)
{
  return -1;
}

static int
fla_fuse_cb_mknod(const char *path, mode_t mode, dev_t rdev)
{
  return -EROFS;
}

static int
fla_fuse_cb_mkdir(const char *path, mode_t mode)
{
  return -EROFS;
}

static int
fla_fuse_cb_unlink(const char *path)
{
  return -EROFS;
}

static int
fla_fuse_cb_rmdir(const char *path)
{
  return -EROFS;
}

static int
fla_fuse_cb_symlink(const char *from, const char *to)
{
  return -EROFS;
}

static int
fla_fuse_cb_rename(const char *from, const char *to,
                   unsigned int flags)
{
  return -EROFS;
}

static int
fla_fuse_cb_link(const char *from, const char *to)
{
  return -EROFS;
}

static int
fla_fuse_cb_chmod(const char *path, mode_t mode,
                  struct fuse_file_info *fi)
{
  return -EROFS;
}

static int
fla_fuse_cb_chown(const char *path, uid_t uid, gid_t gid,
                  struct fuse_file_info *fi)
{
  return -EROFS;
}

static int
fla_fuse_cb_truncate(const char *path, off_t offset,
                     struct fuse_file_info *fi)
{
  return -EROFS;
}

static int
fla_fuse_cb_open(const char *path, struct fuse_file_info *fi)
{
  return -1;
}

static int
fla_fuse_cb_read(const char *path, char *buf, size_t size,
                 off_t offset, struct fuse_file_info *fi)
{
  return -1;
}

static int
fla_fuse_cb_write(const char *path, const char *buf, size_t size,
                  off_t offset, struct fuse_file_info *fi)
{
  return -EROFS;
}

static int
fla_fuse_cb_statfs(const char *path, struct statvfs *stbuf)
{
  return -1;
}

static int
fla_fuse_cb_flush(const char *path, struct fuse_file_info *fi)
{
  return 0;
}

static int
fla_fuse_cb_release(const char *path, struct fuse_file_info *fi)
{
  return -1;
}

static int
fla_fuse_cb_fsync(const char *path, int foo,
                  struct fuse_file_info *fi)
{
  return 0;
}

static int
fla_fuse_cb_setxattr(const char *path, const char *name,
                     const char *value,
                     size_t size, int flags)
{
  return -EROFS;
}

static int
fla_fuse_cb_getxattr(const char *path, const char *name,
                     char *value, size_t size)
{
  return -1;
}

static int
fla_fuse_cb_listxattr(const char *path, char *name, size_t size)
{
  return -1;
}

static int
fla_fuse_cb_removexattr(const char *path, const char *name)
{
  return -EROFS;
}

static int
fla_fuse_cb_opendir(const char *path, struct fuse_file_info *fi)
{
  return -1;
}

static int
fla_fuse_cb_readdir(const char *path, void *buf,
                    fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi,
                    enum fuse_readdir_flags flags)
{
  return -1;
}

static int
fla_fuse_cb_releasedir(const char *path, struct fuse_file_info *fi)
{
  return -1;
}

static int
fla_fuse_cb_fsyncdir(const char *path, int datasync,
                     struct fuse_file_info *fi)
{
  // there can be nothing to sync, should be OK.
  return 0;
}

// called when filesystem is initialized
static void *
fla_fuse_cb_init(struct fuse_conn_info *conn,
                 struct fuse_config *cfg)
{
  fprintf(stderr, "flexalloc fuse module init...");
  return NULL;
}

static void
fla_fuse_cb_destroy(void *private_date)
{
}

static int
fla_fuse_cb_access(const char *path, int mode)
{
  return -1;
}

static int
fla_fuse_cb_create(const char *path, mode_t mode,
                   struct fuse_file_info *fi)
{
  return -EROFS;
}

static int
fla_fuse_cb_utimens(const char *path, const struct timespec tv[2],
                    struct fuse_file_info *fi)
{
  return -EROFS;
}

static int
fla_fuse_cb_bmap(const char *path, size_t blocksize, uint64_t *idx)
{
  return -1;
}

static int
fla_fuse_cb_ioctl(const char *path, unsigned int cmd, void *arg,
                  struct fuse_file_info *fi, unsigned int flags, void *data)
{
  return -1;
}

static int
fla_fuse_cb_poll(const char *path, struct fuse_file_info *fi,
                 struct fuse_pollhandle *ph, unsigned *reventssp)
{
  return -1;
}

static int
fla_fuse_cb_write_buf(const char *path, struct fuse_bufvec *buf,
                      off_t off, struct fuse_file_info *fi)
{
  return -EROFS;
}

static int
fla_fuse_cb_read_buf(const char *path, struct fuse_bufvec **bufp,
                     size_t size, off_t off, struct fuse_file_info *fi)
{
  return -1;
}

static int
fla_fuse_cb_fallocate(const char *path, int mode, off_t offset,
                      off_t length,
                      struct fuse_file_info *fi)
{
  return -EROFS;
}

static ssize_t
fla_fuse_cb_copy_file_range(const char *path_in,
                            struct fuse_file_info *fi_in,
                            off_t offset_in,
                            const char *path_out,
                            struct fuse_file_info *fi_out,
                            off_t offset_out, size_t size, int flags)
{
  return -EROFS;
}

static off_t
fla_fuse_cb_lseek(const char *path, off_t offset, int whence,
                  struct fuse_file_info *fi)
{
  return -1;
}


static const struct fuse_operations fla_fuse_fs_ops =
{
  .getattr					= fla_fuse_cb_getattr,
  .readlink				= fla_fuse_cb_readlink,
  .mknod						= fla_fuse_cb_mknod,
  .mkdir						= fla_fuse_cb_mkdir,
  .unlink					= fla_fuse_cb_unlink,
  .rmdir						= fla_fuse_cb_rmdir,
  .symlink					= fla_fuse_cb_symlink,
  .rename					= fla_fuse_cb_rename,
  .link 						= fla_fuse_cb_link,
  .chmod						= fla_fuse_cb_chmod,
  .chown						= fla_fuse_cb_chown,
  .truncate				= fla_fuse_cb_truncate,
  .open						= fla_fuse_cb_open,
  .read						= fla_fuse_cb_read,
  .write						= fla_fuse_cb_write,
  .statfs					= fla_fuse_cb_statfs,
  .flush						= fla_fuse_cb_flush,
  .release					= fla_fuse_cb_release,
  .fsync						= fla_fuse_cb_fsync,
  /* Extended attributes support for userland interaction */
  .setxattr				= fla_fuse_cb_setxattr,
  .getxattr				= fla_fuse_cb_getxattr,
  .listxattr				= fla_fuse_cb_listxattr,
  .removexattr			= fla_fuse_cb_removexattr,
  .opendir					= fla_fuse_cb_opendir,
  .readdir					= fla_fuse_cb_readdir,
  .releasedir			= fla_fuse_cb_releasedir,
  .fsyncdir				= fla_fuse_cb_fsyncdir,
  .init						= fla_fuse_cb_init,
  .destroy					= fla_fuse_cb_destroy,
  .access					= fla_fuse_cb_access,
  .create					= fla_fuse_cb_create,
  .utimens					= fla_fuse_cb_utimens,
  .bmap						= fla_fuse_cb_bmap,
  .ioctl						= fla_fuse_cb_ioctl,
  .poll						= fla_fuse_cb_poll,
  .write_buf				= fla_fuse_cb_write_buf,
  .read_buf				= fla_fuse_cb_read_buf,
  .fallocate				= fla_fuse_cb_fallocate,
  .copy_file_range	= fla_fuse_cb_copy_file_range,
  .lseek						= fla_fuse_cb_lseek,
};

int
main(int argc, char **argv)
{
  // struct fla_fuse_opts o;
  fprintf(stdout, "hello, world\n");
  //if (fla_fuse_parse_args(&o, argc, argv) != 0) {
  //    return -1;
  //}
  return fuse_main(argc, argv, &fla_fuse_fs_ops, NULL);
}
