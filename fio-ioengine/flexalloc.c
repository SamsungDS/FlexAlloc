/*
 * flexalloc engine
 *
 * IO engine for the flexalloc file system
 *
 *
 */
#include <stdlib.h>
#include <assert.h>
#include "libflexalloc.h"
#include "flexalloc_daemon_base.h"

#include <fio.h>
#include <optgroup.h>

static pthread_mutex_t fa_mutex = PTHREAD_MUTEX_INITIALIZER;
struct ioengine_ops ioengine;

struct flexalloc_data {
	struct flexalloc *fs;
	struct fla_pool *pool;
	struct fla_object *object;
	struct fla_daemon_client daemon;
};

struct flexalloc_options {
	struct thread_data *td;
	char *daemon_uri;
	char *dev_uri;
	char *md_dev_uri;
	char *poolname;
};

struct fio_option fa_options[] = {
	{
		.name		= "dev_uri",
		.alias		= "dev_uri",
		.lname		= "path to device node",
		.type 		= FIO_OPT_STR_STORE,
		.off1		= offsetof(struct flexalloc_options, dev_uri),
		.help		= "Flexalloc file system device name (e.g., /dev/nvme0n1)",
		.category	= FIO_OPT_C_ENGINE,
		.group		= FIO_OPT_G_INVALID,
	},
	{
		.name		= "md_dev_uri",
		.alias		= "md_dev_uri",
		.lname		= "path to metadata device node",
		.type 		= FIO_OPT_STR_STORE,
		.off1		= offsetof(struct flexalloc_options, md_dev_uri),
		.help		= "Flexalloc file system metadata device name (e.g., /dev/nvme0n2)",
		.category	= FIO_OPT_C_ENGINE,
		.group		= FIO_OPT_G_INVALID,
	},
	{
		.name		= "daemon_uri",
		.alias		= "daemon_uri",
		.lname		= "path to daemon UNIX socket",
		.type 		= FIO_OPT_STR_STORE,
		.off1		= offsetof(struct flexalloc_options, daemon_uri),
		.help		= "Flexalloc file system metadata device name (e.g., /tmp/flexalloc.socket)",
		.category	= FIO_OPT_C_ENGINE,
		.group		= FIO_OPT_G_INVALID,
	},
	{
		.name		= "poolname",
		.lname		= "Pool name prefix",
		.type		= FIO_OPT_STR_STORE,
		.help		= "Flexalloc file system pool name prefix (optional)",
		.off1		= offsetof(struct flexalloc_options, poolname),
		.category	= FIO_OPT_C_ENGINE,
		.group		= FIO_OPT_G_FILENAME,
	},
	{
		.name	= NULL,
	},
};

static int daemon_mode(struct flexalloc_options *opts)
{
	return opts->daemon_uri != NULL;
}

static enum fio_q_status fio_flexalloc_queue(struct thread_data *td,
					struct io_u *io_u)
{
	struct flexalloc_data *fad = td->io_ops_data;
	struct fio_file *f = io_u->file;
	int ret = 0;

	fio_ro_check(td, io_u);

	io_u->error = 0;

	switch (io_u->ddir) {
	case DDIR_READ:
		ret = fla_object_read(fad->fs, fad->pool, &fad->object[f->fileno],
				io_u->xfer_buf, io_u->offset, io_u->xfer_buflen);
		break;
	case DDIR_WRITE:
		ret = fla_object_write(fad->fs, fad->pool, &fad->object[f->fileno],
				io_u->xfer_buf, io_u->offset, io_u->xfer_buflen);
		break;
	case DDIR_SYNC:
	case DDIR_DATASYNC:
	case DDIR_SYNC_FILE_RANGE:
		ret = fla_sync(fad->fs);
		break;
	default:
		log_err("flexalloc: unsupported data direction\n");
		io_u->error = EINVAL;
		break;
	}

	if (ret)
		io_u->error = EIO;

	return FIO_Q_COMPLETED;
}

static int fio_flexalloc_object_open(struct thread_data *td, struct fio_file *f)
{
	struct flexalloc_data *fad = td->io_ops_data;
	int ret = 0;

	assert(f->fileno < td->o.nr_files);

	if (f->engine_pos)
		ret = fla_object_open(fad->fs, fad->pool, &fad->object[f->fileno]);
	else {
		ret = fla_object_create(fad->fs, fad->pool, &fad->object[f->fileno]);
		if (!ret)
			f->engine_pos = 1;
	}

	dprint(FD_FILE, "flexalloc: opening file %s pool %p\n", f->file_name, fad->pool);

	return ret;
}

static int fio_flexalloc_object_close(struct thread_data *td, struct fio_file *f)
{
	/*
	 * This should just be a noop. We should not call fla_object_free()
	 * here because doing so would make the object handle invalid
	 */

	int err;
	struct flexalloc_data *fad = td->io_ops_data;
	err = fla_object_close(fad->fs, fad->pool, &fad->object[f->fileno]);
	dprint(FD_FILE, "flexalloc: closing file %s pool %p\n", f->file_name, fad->pool);

	return err;
}

static void fio_flexalloc_cleanup_direct(struct thread_data *td)
{
	struct flexalloc_data *fad = td->io_ops_data;
	struct flexalloc_options *o = td->eo;
	struct thread_data *td2;
	int i;
	bool found = false;

	pthread_mutex_lock(&fa_mutex);

	if (fad && fad->fs) {
		for_each_td(td2, i) {
			struct thread_options *o2 = &td2->o;
			struct flexalloc_options *fao2 = td2->eo;
			struct flexalloc_data *fad2 = td2->io_ops_data;

			dprint(FD_IO, "flexalloc: cleanup thread_number td=%d, td2=%d\n", td->thread_number, td2->thread_number);
			if (td->thread_number == td2->thread_number)
				continue;

			dprint(FD_IO, "flexalloc: o2->ioengine=%s, ioengine.name=%s\n", o2->ioengine, ioengine.name);
			if (strcmp(o2->ioengine, ioengine.name))
				continue;

			if (!fao2)
				continue;

			if (!fao2->dev_uri)
				continue;

			if (!o->dev_uri)
				continue;

			dprint(FD_IO, "flexalloc: fao2->dev_uri=%s, o->dev_uri=%s\n", fao2->dev_uri, o->dev_uri);
			if (strcmp(fao2->dev_uri, o->dev_uri))
				continue;

			if (fad2) {
				found = true;
				dprint(FD_IO, "flexalloc: cleanup found a match!\n");
				break;
			}
		}

		if (!found) {
			dprint(FD_IO, "flexalloc: closing file system\n");
			fla_close(fad->fs);
		}

		free(fad->object);
		free(fad);
	}

	pthread_mutex_unlock(&fa_mutex);
}

static void fio_flexalloc_cleanup_daemon(struct thread_data *td)
{
	struct flexalloc_data *fad = td->io_ops_data;
	if (fad->fs)
		// if we failed to open the device, this field will not be set.
		fla_close(fad->fs);
	free(fad->object);
	free(fad);
}

static void fio_flexalloc_cleanup(struct thread_data *td)
{
	struct flexalloc_options *o = td->eo;
	if (daemon_mode(o))
		fio_flexalloc_cleanup_daemon(td);
	else
		fio_flexalloc_cleanup_direct(td);
}

/*
 * This is a wrapper around fla_open(). Since we should open a file system
 * once only this will scan through all the other threads looking for an
 * ioengine and dev_uri match. If a match is found and that thread has already
 * opened the FS use its FS handle instead of acquiring a new one. Otherwise
 * just call the library routine.
 *
 * FS open and pool creation are protected by a mutex even if they target
 * different devices.
 */
static int fio_flexalloc_open_direct(const char *dev_uri, const char *md_dev_uri,
    int thread_number, struct flexalloc **fs)
{
	struct thread_data *td;
	int i;

	for_each_td(td, i) {
		struct thread_options *o = &td->o;
		struct flexalloc_options *fao = td->eo;
		struct flexalloc_data *fad = td->io_ops_data;

		dprint(FD_IO, "flexalloc: thread_number self=%d, td=%d\n", thread_number, td->thread_number);
		if (thread_number == td->thread_number)
			continue;

		dprint(FD_IO, "flexalloc: td_num %d, o->ioengine=%s, ioengine.name=%s\n",
				td->thread_number, o->ioengine, ioengine.name);
		if (strcmp(o->ioengine, ioengine.name))
			continue;

		dprint(FD_IO, "flexalloc: td_num %d, fao->dev_uri=%s, dev_uri=%s\n",
				td->thread_number, fao->dev_uri, dev_uri);
		if (fao->dev_uri && strcmp(fao->dev_uri, dev_uri))
			continue;

		if (!fad)
			continue;

		if (fad->fs) {
			*fs = fad->fs;
			dprint(FD_IO, "flexalloc: using previously obtained file system handle for %s\n", dev_uri);
			return 0;
		}
	}

	struct fla_open_opts open_opts = {0};
	open_opts.dev_uri = dev_uri;
	if(md_dev_uri)
		open_opts.md_dev_uri = md_dev_uri;
	return fla_open(&open_opts, fs);
}

static int fio_flexalloc_open_daemon(struct flexalloc_data *data, struct flexalloc_options *opts)
{
	int err = 0;
	err = fla_daemon_open(opts->daemon_uri, &data->daemon);
	if (err) {
		log_err("flexalloc: failed to open daemon socket '%s'\n", opts->daemon_uri);
		return err;
	}
	data->fs = data->daemon.flexalloc;
	return 0;
}

/*
 * Open the file system and acquire pool handle
 */
static int fio_flexalloc_init(struct thread_data *td)
{
	struct flexalloc_data *fad;
	struct flexalloc_options *o = td->eo;
	int ret = 0;
	char pool_name[PATH_MAX];

	if (td->o.file_size_low != td->o.file_size_high) {
		log_err("flexalloc: filesize cannot be a range\n");
		ret = 1;
	}

	if (!td->o.odirect) {
		log_err("flexalloc: direct=1 must be set for the flexalloc ioengine\n");
		ret = 1;
	}

	/* threads are required when not in daemon mode */
	if (!td->o.use_thread && !o->daemon_uri) {
		log_err("flexalloc: thread=1 must be set for the flexalloc ioengine\n");
		ret = 1;
	}

	if (o->daemon_uri && (o->dev_uri || o->md_dev_uri))
	{
		log_err("flexalloc: do not specify device path(s) if connecting to a daemon\n");
		ret = 1;
	}

	if (o->md_dev_uri && !o->dev_uri)
	{
		log_err("flexalloc: cannot specify a metadata device without also specifying the data device (`dev_uri`)\n");
		ret = 1;
	}

	if (ret)
		return ret;

	fad = calloc(1, sizeof(*fad));
	assert(fad);
	td->io_ops_data = fad;

	fad->object = calloc(td->o.nr_files, sizeof(*fad->object));
	assert(fad->object);

	if (daemon_mode(o))
	{
		ret = fio_flexalloc_open_daemon(fad, o);
	} else {
		pthread_mutex_lock(&fa_mutex);
		ret = fio_flexalloc_open_direct(o->dev_uri, o->md_dev_uri, td->thread_number, &fad->fs);
	}
	if (ret) {
		log_err("flexalloc: unable to open file system\n");
		goto done;
	}

	/*
	 * Assign pool name based on subjob number:
	 * 	poolname.0, poolname.1, ...
	 * If poolname option was not specified use the jobname
	 */

	if (o->poolname)
		snprintf(pool_name, PATH_MAX, "%s.%d", o->poolname, td->subjob_number);
	else
		snprintf(pool_name, PATH_MAX, "%s.%d", td->o.name, td->subjob_number);

	ret = fla_pool_create(fad->fs, pool_name, strlen(pool_name),
			td->o.file_size_low/fla_fs_lb_nbytes(fad->fs), &fad->pool);
	if (ret) {
		log_err("flexalloc: error creating pool %s\n", pool_name);
		goto done;
	}

	dprint(FD_FILE, "flexalloc: created pool %p with poolname %s\n", &fad->pool, pool_name);

done:
	if (!daemon_mode(o))
		pthread_mutex_unlock(&fa_mutex);
	return ret;
}

static int fio_flexalloc_get_file_size(struct thread_data *td, struct fio_file *f)
{
	struct thread_options *o = &td->o;

	if (fio_file_size_known(f))
		return 0;

	f->real_file_size = o->file_size_low;
	fio_file_set_size_known(f);

	return 0;
}

struct ioengine_ops ioengine = {
	.name		= "flexalloc",
	.version	= FIO_IOOPS_VERSION,
	.queue		= fio_flexalloc_queue,
	.init		= fio_flexalloc_init,
	.cleanup	= fio_flexalloc_cleanup,
	.open_file	= fio_flexalloc_object_open,
	.close_file	= fio_flexalloc_object_close,
	.get_file_size	= fio_flexalloc_get_file_size,
	.flags		= FIO_SYNCIO | FIO_DISKLESSIO,
	.options	= fa_options,
	.option_struct_size = sizeof(struct flexalloc_options),
};

static void fio_init fio_flexalloc_register(void)
{
	register_ioengine(&ioengine);
}

static void fio_exit fio_flexalloc_unregister(void)
{
	unregister_ioengine(&ioengine);
}
