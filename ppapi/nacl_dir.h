#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>
#include <stdio.h>
#include <string>

struct NACL_DIR;

struct file_info_t {
  std::string real_name;
  int64_t start;
  int64_t size;
};

void lazy_init_dirs(void);
file_info_t lookup_file(const char* fname);


NACL_DIR* nacl_opendir(const char* fname);
struct dirent* nacl_readdir(NACL_DIR* dirp);
int nacl_closedir(NACL_DIR* dirp);
int nacl_stat(const char *path, struct stat *buf);
