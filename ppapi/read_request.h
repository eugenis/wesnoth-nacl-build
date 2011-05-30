#include <string>
#include <vector>
#include <string.h>

#include <ppapi/cpp/instance.h>
#include <ppapi/cpp/module.h>
#include <ppapi/cpp/rect.h>
#include <ppapi/cpp/size.h>

#include <ppapi/c/ppb_url_loader.h>
#include <ppapi/c/ppb_url_request_info.h>
#include <ppapi/c/ppb_url_response_info.h>
#include <ppapi/c/dev/ppb_file_io_dev.h>
#include <ppapi/c/dev/ppb_var_deprecated.h>
#include <ppapi/c/pp_completion_callback.h>
#include "ppapi/c/pp_bool.h"
#include "ppapi/c/pp_errors.h"

class ReadRequest {
 public:
  ReadRequest(PP_Instance instance) : instance_(instance) {
    pthread_mutex_init(&mu_, NULL);
    pthread_cond_init(&cv_, NULL);
  }
  bool Start();
  bool Wait();

  PP_Resource fileio; // in
  size_t offset; // in
  size_t count; // in
  char* buf; // in
  size_t read_count; // out
  int errno_; // out

  void Start_MainThread();
  void ReadFileBodyCallback(int32_t pp_error);

 private:
  PP_Instance instance_;
  pthread_mutex_t mu_;
  pthread_cond_t cv_;

  const PPB_FileIO_Dev* fileio_interface_;

  void Success();
  void Fail();

  bool done_;
};
