// Copyright (c) 2022-present, Adam Manzanares <a.manzanares@samsung.com>
// Copyright (c) 2022-present, Joel Granados <j.granados@samsung.com>
// Copyright (c) 2022-present, Jesper Devantier <j.devantier@samsung.com>

#pragma GCC diagnostic ignored "-Wunused-parameter"
#include "env_flan.h"

#include <bits/stdint-uintn.h>
#include <memory>
#include <chrono>
#include <cstring>
#include <sstream>
#include <rocksdb/utilities/object_registry.h>

namespace ROCKSDB_NAMESPACE {
#define ROCKSDB_POOLNAME "ROCKSDB_POOL"

#ifndef ROCKSDB_LITE

extern "C" FactoryFunc<Env> flan_reg;

ObjectLibrary::PatternEntry flan_pattern = ObjectLibrary::PatternEntry("flan").AddSeparator(":");
FactoryFunc<Env> flan_reg =
  ObjectLibrary::Default()->AddFactory<Env>(flan_pattern,
      [](const std::string &flan_opts, std::unique_ptr<Env>* env, std::string *) {
      *env = NewFlanEnv(flan_opts);
      return env->get();
      });

#endif // ROCKSDB_LITE

void split(const std::string &in, char delim, std::vector<std::string> &pieces)
{
  pieces.clear();
  size_t p_start = 0;
  size_t p_end = in.find(delim);
  while (p_end != std::string::npos) {
    std::string subpiece = in.substr(p_start, p_end - p_start);
    pieces.push_back(subpiece);
    p_start = p_end + 1;
    p_end = in.find(delim, p_start);
  }


  pieces.push_back(in.substr(p_start, in.size() - p_start));
}

std::unique_ptr<Env>
NewFlanEnv(const std::string &flan_opts) {
  return std::unique_ptr<Env>(new LibFlanEnv(Env::Default(), flan_opts));
}

LibFlanEnv::LibFlanEnv(Env *env, const std::string &flan_opts)
  : EnvWrapper(env), options(new LibFlanEnv_Options())
{
  char const * md_dev_uri_ptr = NULL;
  uint64_t obj_nbytes;
  uint32_t strp_nobjs = 0, strp_nbytes = 0;
  std::vector<std::string> opts;
  std::vector<std::string> opts_names =
  {"flan", "main device", "metadata device",
    "object_nbytes", "strp_nobjs", "strp_nbytes"};

  options->flan_mut = new std::mutex();
  /* Order of the options:
   * [0] flan
   * [1] main device
   * [2] metadata device. might be ""
   * [3] object nbytes
   * [4] strp_nobjs
   * [5] strp_nbytes
   *
   * if 4 or 5 are "" then we create an unstriped run
   */
  split(flan_opts, ':', opts);
  for(size_t i = 0 ; i < opts.size() ; ++i)
    std::cout << "option [" << opts_names[i] << "] : " << opts[i] << std::endl;

  // make sure we have all the arguments
  if (opts.size() != 6)
    throw std::runtime_error(std::string("Error : missing a parameter in flan plugin string"));

  // make sure we are in flan
  if (opts[0].compare("flan") != 0)
    throw std::runtime_error(std::string("Error : initializing a plugin that is not flan"));

  dev_uri = opts[1];
  if (opts[2].size() > 0)
  {
    md_dev_uri = opts[2];
    md_dev_uri_ptr = md_dev_uri.c_str();
  }


  // Get object size in bytes
  std::istringstream obj_nbytes_stream(opts[3]);
  obj_nbytes_stream >> obj_nbytes;

  std::string p_name = ROCKSDB_POOLNAME;
  struct fla_pool_create_arg pool_arg =
  {
    .flags = 0,
    .name = (char*)p_name.c_str(),
    .name_len = (int)p_name.length(),
    .obj_nlb = 0, // will get set by flan_init
    .strp_nobjs = 0,
    .strp_nbytes = 0
  };

  if (opts[4].size() > 0 && opts[5].size() > 0) {
    // Get strp_nobjs
    std::istringstream strp_nobjs_stream(opts[4]);
    strp_nobjs_stream >> strp_nobjs;
    pool_arg.strp_nobjs = strp_nobjs;

    // Get strp_nbytes
    std::istringstream strp_nbytes_stream(opts[5]);
    strp_nbytes_stream >> strp_nbytes;
    pool_arg.strp_nbytes = strp_nbytes;

    pool_arg.flags |= FLA_POOL_ENTRY_STRP;
  }

  //std::cout << "Starting a flan environmnet" << std::endl;
  int ret = flan_init(dev_uri.c_str(), md_dev_uri_ptr, &pool_arg, obj_nbytes, &flanh);
  if (ret)
      throw std::runtime_error(std::string("Error initializing flan: error ") + 
          std::to_string(ret));
}

LibFlanEnv::~LibFlanEnv()
{
  //std::cout << "Executing the destructor function ===================================" << std::endl;
  //std::lock_guard<std::mutex> guard(*(options->flan_mut));
  //flan_close(this->flanh); // TODO flan close returns status so should flan_close
}

Status
LibFlanEnv::NewSequentialFile(
    const std::string& fname, std::unique_ptr<SequentialFile>* result, const EnvOptions& o)
{
  Status s;
  s = FileExists(fname);
  if (!s.ok())
    return s;
  result->reset(new LibFlanEnvSeqAccessFile(fname, options, flanh));
  return Status::OK();
}

Status
LibFlanEnv::NewRandomAccessFile(
    const std::string& fname, std::unique_ptr<RandomAccessFile>* result, const EnvOptions& o)
{
  result->reset(new LibFlanEnvRandAccessFile(fname, options, flanh));
  return Status::OK();
}

Status
LibFlanEnv::NewWritableFile(
    const std::string& fname, std::unique_ptr<WritableFile>* result, const EnvOptions& o)
{
  result->reset(new LibFlanEnvWriteableFile(fname, options, flanh));
  return Status::OK();
}

Status
LibFlanEnv::NewDirectory(const std::string& name, std::unique_ptr<Directory>* result)
{
  result->reset(new LibFlanEnvDirectory());
  return Status::OK();
}

Status
LibFlanEnv::FileExists(const std::string& fname)
{
  uint64_t oh;
  std::lock_guard<std::mutex> guard(*(options->flan_mut));
  if(!flan_object_open(fname.c_str(), flanh, &oh, FLAN_OPEN_FLAG_READ)) {
    flan_object_close(oh, flanh);
    return Status::OK();
  }
  else
    return Status::NotFound(Status::SubCode::kPathNotFound, fname);
}
Status
LibFlanEnv::GetChildren(const std::string& path, std::vector<std::string>* result)
{
  return Status::OK();
}

Status
LibFlanEnv::DeleteFile(const std::string& fname)
{
  std::lock_guard<std::mutex> guard(*(options->flan_mut));
  if(flan_object_delete(fname.c_str(), flanh))
    return Status::NotFound(Status::SubCode::kNone, fname);

  return Status::OK(); // No matter what happens we return that the file is not there
}

Status
LibFlanEnv::CreateDir(const std::string& name)
{
  throw;
}

Status
LibFlanEnv::CreateDirIfMissing(const std::string& name)
{
  //std::cout << "Calling " << __func__ << ", fname " << name << std::endl;
  return Status::OK();
}

  Status
LibFlanEnv::DeleteDir(const std::string& name)
{
  //std::cout << "Calling " << __func__ << ", fname " << name << std::endl;
  return Status::OK();
}

Status
LibFlanEnv::GetFileSize(const std::string& fname, uint64_t* size)
{
  std::lock_guard<std::mutex> guard(*(options->flan_mut));
  uint32_t res_cur;
  struct flan_oinfo *oinfo = flan_find_oinfo(flanh, fname.c_str(), &res_cur);
  
  /*
   * When I don't find the files I sould returned not found, but rocks expects
   * a size zero. WHY!!!!!!!!
    //    return Status::NotFound(Status::SubCode::kPathNotFound, fname);
   */
  if(!oinfo)
    *size = 0;
  else
    *size = oinfo->size;
  return Status::OK();
}

  Status
LibFlanEnv::GetFileModificationTime(const std::string& fname, uint64_t* file_mtime)
{
  throw;
}

Status
LibFlanEnv::RenameFile(const std::string& src, const std::string& target)
{
  std::lock_guard<std::mutex> guard(*(options->flan_mut));
  if (flan_object_rename(src.c_str(), target.c_str(), flanh))
    return Status::NotFound(Status::SubCode::kNone, src);

  return Status::OK();
}

Status
LibFlanEnv::LinkFile(const std::string& src, const std::string& target)
{
  return Status::NotSupported("Flan does not support hard linking");
}
Status
LibFlanEnv::LockFile(const std::string& fname, FileLock** lock)
{
  //std::cout << "Calling " << __func__ << ", fname " << fname << std::endl;
  return Status::OK();
}
Status
LibFlanEnv::UnlockFile(FileLock* lock)
{
  //std::cout << "Calling " << __func__ << ", " << __FILE__ << __LINE__ << std::endl;
  return Status::OK();
}
Status
LibFlanEnv::GetAbsolutePath(const std::string& db_path, std::string* output_path)
{
  //std::cout << "Calling " << __func__ << ", fname " << db_path << std::endl;
  return Status::OK();
}
Status
LibFlanEnv::GetTestDirectory(std::string* path)
{
  //std::cout << __FUNCTION__ << std::endl;
  return Status::OK();
}
Status
LibFlanEnv::IsDirectory(const std::string& path, bool* is_dir)
{
  //std::cout << __FUNCTION__ << std::endl;
  throw;
}
Status
LibFlanEnv::NewLogger(const std::string& fname, std::shared_ptr<Logger>* result)
{
  //std::cout << "Calling " << __func__ << ", fname " << fname << std::endl;
  return Status::OK();
}

Status
LibFlanEnv::close()
{
  //std::cout << "Executing the close function ===================================" << std::endl;
  //std::lock_guard<std::mutex> guard(*(options->flan_mut));
  //flan_close(this->flanh); // TODO flan close returns status so should flan_close

  return Status::OK();
}

Status
LibFlanEnv::fssync()
{
  std::lock_guard<std::mutex> guard(*(options->flan_mut));
  if(flan_sync(flanh) != 0)
  {
    std::cerr << "Aborting flushing ... " << std::endl;
	  return Status::Aborted();
  }
  return Status::OK();
}

LibFlanEnvSeqAccessFile::LibFlanEnvSeqAccessFile(
    const std::string & fname_, std::shared_ptr<LibFlanEnv::LibFlanEnv_Options>& options, struct flan_handle *flanh_)
  :fname(fname_), flanh(flanh_), fh(NULL)
{
  lnfs_mut = options->flan_mut;
  std::lock_guard<std::mutex> guard(*lnfs_mut);
  fh = std::make_shared<LibFlanEnv::LibFlanEnv_F>();
  fh->flanh = flanh;
  int ret = flan_object_open(fname.c_str(), flanh, &fh->object_handle, FLAN_OPEN_FLAG_READ);
  if(ret)
    throw std::runtime_error(std::string("Error finding file:") + fname +
        std::string(" error: ") + std::to_string(ret));

  fh->opens++;
}

LibFlanEnvSeqAccessFile::~LibFlanEnvSeqAccessFile(){}

Status
LibFlanEnvSeqAccessFile::Read(size_t n, Slice* result, char* scratch)
{
  ssize_t len;
  std::lock_guard<std::mutex> guard(*lnfs_mut);

  len = flan_object_read(fh->object_handle, scratch, fh->roffset_, n, fh->flanh);
  if (len < 0)
    return Status::IOError();

  *result = Slice(scratch, len);
  fh->roffset_ += len;

  return Status::OK();
}

Status
LibFlanEnvSeqAccessFile::PositionedRead(uint64_t offset, size_t n, Slice* result, char* scratch)
{
  ssize_t len;
  std::lock_guard<std::mutex> guard(*lnfs_mut);
  
  len = flan_object_read(fh->object_handle, scratch, offset, n, fh->flanh);
  if (len < 0)
    return Status::IOError();

  result->size_ = len;
  result->data_ = scratch;
  return Status::OK();
}

/*bool
LibFlanEnvSeqAccessFile::use_direct_io() const
{
  return true;
}*/

Status
LibFlanEnvSeqAccessFile::Skip(uint64_t n)
{
  std::lock_guard<std::mutex> guard(*lnfs_mut);
  if(fh->roffset_ + n > fh->e_o_f_)
    return Status::IOError();
  fh->roffset_ += n;
  return Status::OK();
}

LibFlanEnvRandAccessFile::LibFlanEnvRandAccessFile(
    const std::string & fname_, std::shared_ptr<LibFlanEnv::LibFlanEnv_Options>& options, struct flan_handle *flanh_)
  :fname(fname_), flanh(flanh_), fh(NULL)
{
  //std::cout << "Rand Access File:" << fname << std::endl;
  lnfs_mut = options->flan_mut;
  std::lock_guard<std::mutex> guard(*lnfs_mut);
  fh = std::make_shared<LibFlanEnv::LibFlanEnv_F>();
  fh->flanh = flanh;
  int ret = flan_object_open(fname.c_str(), flanh, &fh->object_handle, FLAN_OPEN_FLAG_READ);
  if(ret)
    throw std::runtime_error(std::string("Error allocating object for file:") + fname +
        std::string(" error: ") + std::to_string(ret));

  fh->opens++;
}
LibFlanEnvRandAccessFile::~LibFlanEnvRandAccessFile(){}

Status
LibFlanEnvRandAccessFile::Read(uint64_t offset, size_t n, Slice* result, char* scratch) const
{
  ssize_t len;
  std::lock_guard<std::mutex> guard(*lnfs_mut);

  len = flan_object_read(fh->object_handle, scratch, offset, n, fh->flanh);
  if (len < 0)
    return Status::IOError();

  *result = Slice(scratch, len);
  return Status::OK();
}
Status
LibFlanEnvRandAccessFile::Prefetch(uint64_t offset, size_t n)
{
  return Status::OK();
}
Status
LibFlanEnvRandAccessFile::MultiRead(ReadRequest* reqs, size_t num_reqs)
{
  //std::cout << "Multi read " << std::endl;
  throw;
}
size_t
LibFlanEnvRandAccessFile::GetUniqueId(char* id, size_t max_size) const
{
  return 0;
};
/*bool
LibFlanEnvRandAccessFile::use_direct_io() const
{
  return true;
};*/
Status
LibFlanEnvRandAccessFile::InvalidateCache(size_t offset, size_t length)
{
  throw;
};

LibFlanEnvWriteableFile::LibFlanEnvWriteableFile(
    const std::string & fname_, std::shared_ptr<LibFlanEnv::LibFlanEnv_Options>& options, struct flan_handle *flanh_)
  :fname(fname_), flanh(flanh_), fh(NULL), _prevwr(0)
{

  lnfs_mut = options->flan_mut;
  std::lock_guard<std::mutex> guard(*lnfs_mut);
  fh = std::make_shared<LibFlanEnv::LibFlanEnv_F>();
  fh->flanh = flanh;

  int ret = flan_object_open(fname.c_str(), flanh, &fh->object_handle, FLAN_OPEN_FLAG_CREATE |
                             FLAN_OPEN_FLAG_WRITE);
  if(ret)
    throw std::runtime_error(std::string("Error allocating object for file:") + fname +
        std::string(" error: ") + std::to_string(ret));
  fh->opens++;
}

LibFlanEnvWriteableFile::~LibFlanEnvWriteableFile()
{
}

Status
LibFlanEnvWriteableFile::Append(const Slice& slice)
{
  std::lock_guard<std::mutex> guard(*lnfs_mut);

  int ret = flan_object_write(fh->object_handle, (void*)slice.data_, fh->woffset_,
      slice.size_, fh->flanh);
  if(ret)
    throw std::runtime_error(std::string("Error appending to file: ") + fname +
        std::string(" error: ") + std::to_string(ret));

  fh->e_o_f_ += slice.size_;
  fh->woffset_ += slice.size_;
  return Status::OK();
}

Status
LibFlanEnvWriteableFile::PositionedAppend(const Slice& slice, uint64_t offset)
{
  std::lock_guard<std::mutex> guard(*lnfs_mut);

  int ret = flan_object_write(fh->object_handle, (void*)slice.data_, offset,
      slice.size_, fh->flanh);
  if(ret)
    throw std::runtime_error(std::string("Error pos appending to file :") + fname +
        std::string(" error: ") + std::to_string(ret));

  fh->e_o_f_ = offset + slice.size_ > fh->e_o_f_ ? offset + slice.size_ : fh->e_o_f_;
  fh->woffset_ = offset + slice.size_;
  return Status::OK();
}

Status
LibFlanEnvWriteableFile::PositionedAppend(const Slice& s, uint64_t o, const DataVerificationInfo&)
{
  return PositionedAppend(s, o);
}

Status
LibFlanEnvWriteableFile::Append(const Slice& s, const DataVerificationInfo&)
{
  return Append(s);
}
/*
bool
LibFlanEnvWriteableFile::use_direct_io() const
{
  return true;
}*/

Status
LibFlanEnvWriteableFile::Flush()
{
  return Status::OK();
}
Status
LibFlanEnvWriteableFile::Sync()
{
  std::lock_guard<std::mutex> guard(*lnfs_mut);
  int ret = flan_sync(fh->flanh);
  if(ret != 0)
    throw std::runtime_error(std::string("Error syncing writable file : ") + fname +
        std::string(" error: ") + std::to_string(ret));
  return Status::OK();
}
Status
LibFlanEnvWriteableFile::Close()
{
  std::lock_guard<std::mutex> guard(*lnfs_mut);
  flan_object_close(fh->object_handle, fh->flanh);
  return Status::OK();
}

LibFlanEnvDirectory::LibFlanEnvDirectory(){}
LibFlanEnvDirectory::~LibFlanEnvDirectory(){}
Status LibFlanEnvDirectory::Fsync(){return Status::OK();}

}
