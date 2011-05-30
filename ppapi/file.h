#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

void naclfs_set_plugin_instance(PP_Instance instance);
// int weak_open(const char* path, int flags, ...);
// ssize_t read(int fd, void *buf, size_t count);
// off_t lseek(int fd, off_t offset, int whence);
// int close(int fd);

#ifdef __cplusplus
}
#endif  /* __cplusplus */
