#include <map>
#include <stdio.h>
#include <errno.h>

#include <sys/types.h>
#include <unistd.h>

#include "open_request.h"
#include "read_request.h"
#include "close_request.h"
#include "nacl_dir.h"
#include "file.h"

extern "C" {

  typedef int64_t nacl_abi__off_t;
  typedef nacl_abi__off_t nacl_abi_off_t;

  int weak_open(const char* path, int flags, int mode);
  ssize_t sys_read(int fd, void *buf, size_t count);
  off_t sys_lseek(int fd, nacl_abi_off_t* nacl_offset, int whence);
  int sys_close(int fd);

  ssize_t __libc_sys_read(int fd, void *buf, size_t size);
  off_t __libc_sys_lseek(int fd, nacl_abi_off_t* nacl_offset, int whence);
  int __libc_sys_close(int fd);
}

class Mutex {
 public:
  Mutex() {
    pthread_mutex_init(&mu_, NULL);
  }
  ~Mutex() {
    pthread_mutex_destroy(&mu_);
  }
  void Lock() {
    pthread_mutex_lock(&mu_);
  }
  void Unlock() {
    pthread_mutex_unlock(&mu_);
  }
 private:
  pthread_mutex_t mu_;
};

class MutexLock {
 public:
  MutexLock(Mutex& mu) : mu_(mu) {
    mu_.Lock();
  }
  ~MutexLock() {
    mu_.Unlock();
  }
 private:
  Mutex& mu_;
};

struct FileDesc {
  PP_Resource fileio;
  int64_t start;
  int64_t size;
  int64_t pos;
  Mutex mu;
};

class DescTbl {
 public:
  DescTbl() {
  }

  int allocate() {
    MutexLock lock(mu_);
    for (size_t i = 1000000; ; ++i) {
      if (tbl_.find(i) == tbl_.end()) {
        tbl_[i] = FileDesc();
        return i;
      }
    }
    assert(0 && "impossible");
  }

  FileDesc* get(int fd) {
    MutexLock lock(mu_);
    std::map<int, FileDesc>::iterator it = tbl_.find(fd);
    if (it != tbl_.end()) {
      return &(it->second);
    } else {
      return NULL;
    }
  }

  void remove(int fd) {
    MutexLock lock(mu_);
    tbl_.erase(fd);
  }
 private:
  std::map<int, FileDesc> tbl_;
  Mutex mu_;
  // int fake_fd;
};

DescTbl descTbl;
PP_Instance globalPluginInstance;

void naclfs_set_plugin_instance(PP_Instance instance) {
  globalPluginInstance = instance;
}

int weak_open(const char* path, int flags, int mode) {
  // TODO: check flags
  fprintf(stderr, "open(%s) ...\n", path);
  file_info_t fileInfo = lookup_file(path);
  if (fileInfo.real_name.empty()) {
    errno = ENOENT;
    return -1;
  }

  fprintf(stderr, "lookup: %s -> %s, %d, %d\n", path, fileInfo.real_name.c_str(), fileInfo.start, fileInfo.size);

  OpenRequest req(globalPluginInstance);
  req.path = fileInfo.real_name;
  // fprintf(stderr, "starting\n");
  req.Start();
  // fprintf(stderr, "waiting\n");
  req.Wait();
  // fprintf(stderr, "done: %d\n", req.errno_);
  if (!req.errno_) {
    int fd = descTbl.allocate();
    FileDesc* fileDesc = descTbl.get(fd);
    MutexLock lock(fileDesc->mu);
    fileDesc->fileio = req.fileio; // TODO: AddRef?
    fileDesc->start = fileInfo.start;
    fileDesc->size = fileInfo.size; // unknown
    fileDesc->pos = 0;
    errno = 0;
    fprintf(stderr, "open(%s) = %d (%lld +%lld)\n", path, fd, fileDesc->start, fileDesc->size);
    return fd;
  } else {
    errno = req.errno_;
    fprintf(stderr, "open(%s) failed, errno %d\n", path, errno);
    return -1;
  }
}

ssize_t sys_read(int fd, void *buf, size_t count) {
  FileDesc* fileDesc = descTbl.get(fd);
  if (!fileDesc)
    return __libc_sys_read(fd, buf, count);
  fprintf(stderr, "read(%d, %d) ...\n", fd, count);
  MutexLock lock(fileDesc->mu);
  ReadRequest req(globalPluginInstance);
  req.fileio = fileDesc->fileio;
  req.offset = fileDesc->pos;
  req.buf = (char*)buf;
  req.count = count;
  // fprintf(stderr, "starting\n");
  req.Start();
  // fprintf(stderr, "waiting\n");
  req.Wait();
  // fprintf(stderr, "done: %d, %d bytes\n", req.errno_, req.read_count);

  ssize_t res;
  if (req.errno_) {
    res = -req.errno_;
  } else {
    res = req.read_count;
    fileDesc->pos += req.read_count;
  }

  fprintf(stderr, "read(%d, %d) = %d\n", fd, count, res);

  return res;
}

off_t sys_lseek(int fd, nacl_abi_off_t* nacl_offset, int whence) {
  FileDesc* fileDesc = descTbl.get(fd);
  if (!fileDesc)
    return __libc_sys_lseek(fd, nacl_offset, whence);
  MutexLock lock(fileDesc->mu);
  switch (whence) {
    case SEEK_SET:
      fileDesc->pos = *nacl_offset;
      break;
    case SEEK_CUR:
      fileDesc->pos += *nacl_offset;
      break;
    case SEEK_END:
    default:
      if (fileDesc->size >= 0)
        fileDesc->pos = fileDesc->size - *nacl_offset; // TODO: clip
      else
        assert(0 && "lseek(..., SEEK_END) unimplemented");
  }
  fprintf(stderr, "lseek(%d, %lld, %d) = %lld\n", fd, *nacl_offset, whence, fileDesc->pos);
  *nacl_offset = fileDesc->pos;
  return 0;
}

int sys_close(int fd) {
  // TODO: do we need to destroy PP_Resource here?
  FileDesc* fileDesc = descTbl.get(fd);
  if (!fileDesc)
    return __libc_sys_close(fd);
  fprintf(stderr, "close(%d)\n", fd);
  MutexLock lock(fileDesc->mu);
  CloseRequest req(globalPluginInstance);
  req.fileio = fileDesc->fileio;
  // fprintf(stderr, "starting\n");
  req.Start();
  // fprintf(stderr, "waiting\n");
  req.Wait();
  descTbl.remove(fd);
  if (req.errno_)
    fprintf(stderr, "close failed, %d\n", req.errno_);
  return -req.errno_;
}
