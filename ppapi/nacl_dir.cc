#include "nacl_dir.h"

#include <string>
#include <string.h>
#include <thread.hpp>

#include <map>
#include <set>
#include <vector>

typedef std::map<std::string, file_info_t> entries_t; // name -> file info
typedef std::map<std::string, entries_t> dirs_t; // dirname -> set of files

struct NACL_DIR {
  entries_t* entries;
  entries_t::iterator next;
  struct dirent dirent_;
};


static dirs_t dirs;
static threading::mutex dirs_mutex;

static std::string normalize_path(std::string path) {
  size_t first = 0;
  std::vector<std::string> parts;
  while (first < path.size()) {
    size_t next = path.find('/', first);
    if (next == std::string::npos)
      next = path.size();
    std::string part = path.substr(first, next - first);
    first = next + 1;
    if (part == "..") {
      if (parts.empty()) {
	fprintf(stderr, "invalid path\n");
	return "";
      } else {
	parts.pop_back();
      }
    } else if (part != "." && !part.empty()) {
      parts.push_back(part);
    }
  }
  std::string res;
  for (unsigned i = 0; i < parts.size(); ++i) {
    res += "/";
    res += parts[i];
  }
  return res;
}

void split_path(std::string path, std::string* dirname, std::string* basename) {
  size_t index = path.rfind("/");
  if (index == std::string::npos)
    index = 0;
  *dirname = path.substr(0, index);
  *basename = path.substr(index + 1, std::string::npos);
}

void lazy_init_dirs(void) {
  threading::lock lock(dirs_mutex);

  static bool initialized = false;

  if (initialized)
    return;

  FILE* fp = fopen("/.dirs", "r");
  if (!fp) {
    fprintf(stderr, "Could not read the directory index\n");
    exit(1);
  }

  int count = 0;
  const int BUFSIZE = 1000;
  char buf[BUFSIZE];
  while (fgets(buf, BUFSIZE, fp)) {
    // Replace the trailing \n with \0.
    long long start;
    long long size;
    char c_full_name[300];
    char c_real_name[300];
    int res = sscanf(buf, "%300s %lld %lld %300s\n", c_full_name, &start, &size, c_real_name);
    if (res < 1) {
      fprintf(stderr, "bad .dirs line: %s\n", buf);
      continue;
    }
    std::string full_name, real_name;
    full_name = c_full_name;
    if (res < 2) start = 0;
    if (res < 3) size = -1;
    if (res < 4) real_name = full_name; else real_name = c_real_name;

    std::string dirname, basename;
    split_path(full_name, &dirname, &basename);
    dirname = normalize_path(dirname);
    real_name = normalize_path(real_name);

    entries_t& files = dirs[dirname];
    if (!basename.empty()) {
      file_info_t fileInfo;
      fileInfo.real_name = real_name;
      fileInfo.start = start;
      fileInfo.size = size;
      files[basename] = fileInfo;
      count ++;
      fprintf(stderr, "file %s  /  %s: %lld, %lld, %s\n", dirname.c_str(), basename.c_str(), fileInfo.start, fileInfo.size, fileInfo.real_name.c_str());
    }
  }

  fclose(fp);

  fprintf(stderr, "nacl_dir initialized: %d entries\n", count);

  initialized = true;
}

static file_info_t invalid_file_info() {
  file_info_t fileInfo;
  fileInfo.real_name = "";
  fileInfo.start = 0;
  fileInfo.size = -1;
  return fileInfo;
}


file_info_t lookup_file(const char* fname) {
  if (strcmp(fname, "/.dirs") == 0) {
    file_info_t fileInfo;
    fileInfo.real_name = fname;
    fileInfo.start = 0;
    fileInfo.size = -1;
    return fileInfo;
  }

  lazy_init_dirs();

  std::string path = normalize_path(fname);

  size_t last_div = path.rfind('/');
  if (last_div == std::string::npos) {
    fprintf(stderr, "No / in path\n");
    return invalid_file_info();
  }

  std::string dir_name = path.substr(0, last_div);
  dirs_t::iterator d_it = dirs.find(dir_name);
  if (d_it == dirs.end()) {
    // fprintf(stderr, "Dir not found: %s\n", dir_name.c_str());
    return invalid_file_info();
  }

  std::string file_name = path.substr(last_div + 1);
  entries_t& entries = d_it->second;
  entries_t::iterator e_it = entries.find(file_name);
  if (e_it != entries.end()) {
    return e_it->second;
  } else {
    return invalid_file_info();
  }
}



NACL_DIR* nacl_opendir(const char* fname) {
  lazy_init_dirs();

  std::string path = normalize_path(fname);

  fprintf(stderr, "opendir(%s) ...\n", path.c_str());

  if (path.empty())
    return NULL;
  if (path[path.size() - 1] == '/')
    path.resize(path.size() - 1);

  dirs_t::iterator it = dirs.find(path);
  if (it == dirs.end()) {
    fprintf(stderr, "opendir: not found: %s\n", path.c_str());
    return NULL;
  }

  NACL_DIR* dir = new NACL_DIR();
  dir->entries = &it->second;
  dir->next = dir->entries->begin();
  memset(&dir->dirent_, 0, sizeof(dir->dirent_));
  fprintf(stderr, "opendir(%s), success\n", fname);
  return dir;
}

struct dirent* nacl_readdir(NACL_DIR* dirp) {
  if (dirp->next == dirp->entries->end())
    return NULL;
  const std::string& file = dirp->next->first;
  dirp->next++;
  strncpy(dirp->dirent_.d_name, file.c_str(), sizeof(dirp->dirent_.d_name));
  fprintf(stderr, "readdir: %s\n", file.c_str());
  return &(dirp->dirent_);
}

int nacl_closedir(NACL_DIR* dirp) {
  delete dirp;
  return 0;
}

int nacl_stat(const char *cpath, struct stat *buf) {
  lazy_init_dirs();

  std::string path = normalize_path(cpath);

  fprintf(stderr, "stat(%s) ...\n", path.c_str());

  if (path.empty())
    return -1;
  if (path[path.size() - 1] == '/')
    path.resize(path.size() - 1);

  dirs_t::iterator d_it = dirs.find(path);
  if (d_it != dirs.end()) {
    memset(buf, 0, sizeof(*buf));
    buf->st_mode = S_IFDIR | S_IRUSR | S_IXUSR | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH;
    fprintf(stderr, "stat(%s), dir\n", path.c_str());
    return 0;
  }

  size_t last_div = path.rfind('/');
  if (last_div == std::string::npos) {
    fprintf(stderr, "No / in path\n");
    return -1;
  }

  std::string dir_name = path.substr(0, last_div);
  d_it = dirs.find(dir_name);
  if (d_it == dirs.end()) {
    fprintf(stderr, "Dir not found: %s\n", dir_name.c_str());
    return -1;
  }

  std::string file_name = path.substr(last_div + 1);
  entries_t& entries = d_it->second;
  entries_t::iterator e_it = entries.find(file_name);
  if (e_it != entries.end()) {
    memset(buf, 0, sizeof(*buf));
    buf->st_mode = S_IFREG | S_IRUSR | S_IXUSR | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH;
    fprintf(stderr, "stat(%s), file\n", path.c_str());
    return 0;
  }

  fprintf(stderr, "File not found in dir: %s  /  %s\n", dir_name.c_str(), file_name.c_str());

  return -1;
}
