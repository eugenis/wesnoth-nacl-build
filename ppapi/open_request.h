#include <string>
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
#include <ppapi/c/dev/pp_file_info_dev.h>
#include <ppapi/c/pp_bool.h>
#include <ppapi/c/pp_errors.h>

class OpenRequest {
 public:
  OpenRequest(PP_Instance instance) : instance_(instance) {
    pthread_mutex_init(&mu_, NULL);
    pthread_cond_init(&cv_, NULL);
  }
  bool Start();
  bool Wait();

  std::string path; // in
  PP_Resource fileio; // out
  int errno_; // out

  void Start_MainThread();
  void OpenCallback(int32_t pp_error);
  void FinishStreamingToFileCallback(int32_t pp_error);
  void OpenFileBodyCallback(int32_t pp_error);

 private:
  PP_Instance instance_;
  pthread_mutex_t mu_;
  pthread_cond_t cv_;

  PP_Resource request_;
  PP_Resource loader_;
  PP_Resource response_;
  PP_Resource fileio_;
  struct PP_FileInfo_Dev fileInfo_;

  const PPB_URLRequestInfo* request_interface_;
  const PPB_URLResponseInfo* response_interface_;
  const PPB_URLLoader* loader_interface_;
  const PPB_FileIO_Dev* fileio_interface_;
  const PPB_Var_Deprecated* ppb_var_interface_;

  void Success();
  void Fail();

  bool done_;

  PP_Var StrToVar(const char* str) {
    pp::Module* module = pp::Module::Get();
    if (NULL == module)
      return PP_MakeUndefined();
    return ppb_var_interface_->VarFromUtf8(module->pp_module(), str, strlen(str));
  }

};
