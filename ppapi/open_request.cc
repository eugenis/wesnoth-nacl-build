#include "open_request.h"
#include <stdio.h>
#include <errno.h>
#include <ppapi/cpp/completion_callback.h>

static void OpenCallback(void* user_data, int32_t pp_error) {
  // fprintf(stderr, "%s\n", __FUNCTION__);
  OpenRequest* obj = reinterpret_cast<OpenRequest*>(user_data);
  if (NULL != obj)
    obj->OpenCallback(pp_error);
}

static void FinishStreamingToFileCallback(void* user_data, int32_t pp_error) {
  // fprintf(stderr, "%s\n", __FUNCTION__);
  OpenRequest* obj = reinterpret_cast<OpenRequest*>(user_data);
  if (NULL != obj)
    obj->FinishStreamingToFileCallback(pp_error);
}

static void OpenFileBodyCallback(void* user_data, int32_t pp_error) {
  // fprintf(stderr, "%s\n", __FUNCTION__);
  OpenRequest* obj = reinterpret_cast<OpenRequest*>(user_data);
  if (NULL != obj)
    obj->OpenFileBodyCallback(pp_error);
}

static void Start_MainThread(void* user_data, int32_t pp_error) {
  // fprintf(stderr, "%s\n", __FUNCTION__);
  OpenRequest* obj = reinterpret_cast<OpenRequest*>(user_data);
  if (NULL != obj)
    obj->Start_MainThread();
}


void OpenRequest::Start_MainThread() {
  // fprintf(stderr, "%s\n", __FUNCTION__);

  pp::Module* module = pp::Module::Get();

  // fprintf(stderr, "START()\n");
  // fflush(stderr);
  // fsync(fileno(stderr));


  request_interface_ = static_cast<const PPB_URLRequestInfo*>(module->GetBrowserInterface(PPB_URLREQUESTINFO_INTERFACE));
  response_interface_ = static_cast<const PPB_URLResponseInfo*>(module->GetBrowserInterface(PPB_URLRESPONSEINFO_INTERFACE));
  loader_interface_ = static_cast<const PPB_URLLoader*>(module->GetBrowserInterface(PPB_URLLOADER_INTERFACE));
  fileio_interface_ = static_cast<const PPB_FileIO_Dev*>(module->GetBrowserInterface(PPB_FILEIO_DEV_INTERFACE));
  ppb_var_interface_ = static_cast<const PPB_Var_Deprecated*>(module->GetBrowserInterface(PPB_VAR_DEPRECATED_INTERFACE));

  // fprintf(stderr, "interfaces: %p %p %p %p %p\n", request_interface_, response_interface_, loader_interface_, fileio_interface_, ppb_var_interface_);

  request_ = request_interface_->Create(instance_);
  loader_ = loader_interface_->Create(instance_);
  fileio_ = fileio_interface_->Create(instance_);

  // fprintf(stderr, "objects %d %d\n", request_, loader_);

  PP_Bool set_url = request_interface_->SetProperty(request_, PP_URLREQUESTPROPERTY_URL, StrToVar(path.c_str()));
  PP_Bool set_method = request_interface_->SetProperty(request_, PP_URLREQUESTPROPERTY_METHOD, StrToVar("GET"));
  PP_Bool set_file = request_interface_->SetProperty(request_, PP_URLREQUESTPROPERTY_STREAMTOFILE, PP_MakeBool(PP_TRUE));
  assert(set_url == PP_TRUE && set_method == PP_TRUE && set_file == PP_TRUE);

  int32_t pp_error = loader_interface_->Open(loader_,
					     request_,
					     PP_MakeCompletionCallback(::OpenCallback, this));
  assert(pp_error != PP_OK);

  if (pp_error != PP_OK_COMPLETIONPENDING) {  // Asynch failure.
    fprintf(stderr, "open failed %d\n", pp_error);
    Fail();
    return;
  }
}

void OpenRequest::OpenCallback(int32_t pp_error) {
  // fprintf(stderr, "%s\n", __FUNCTION__);
  if (pp_error != PP_OK) {
    fprintf(stderr, "OpenCallback: %d", pp_error);
    Fail();
    return; // fire
  }

  response_ = loader_interface_->GetResponseInfo(loader_);
  if (!response_) {
    Fail();
    return;
  }

  PP_Var status_code =
      response_interface_->GetProperty(response_,
                                       PP_URLRESPONSEPROPERTY_STATUSCODE);
  int32_t status_code_as_int = status_code.value.as_int;
  if (status_code_as_int != 200) {  // Not HTTP OK.
    fprintf(stderr, "bad status code: %d\n", status_code_as_int);
    Fail();
    return;
  }

  pp_error = loader_interface_->FinishStreamingToFile(loader_, PP_MakeCompletionCallback(::FinishStreamingToFileCallback, this));
  if (pp_error == PP_OK) {  // Reached EOF.
    FinishStreamingToFileCallback(pp_error);
  } else if (pp_error != PP_OK_COMPLETIONPENDING) {  // Asynch failure.
    fprintf(stderr, "PPB_URLLoader::FinishStreamingToFile: %d\n", pp_error);
    Fail();
    return;
  }
}

void OpenRequest::FinishStreamingToFileCallback(int32_t pp_error) {
  // fprintf(stderr, "%s\n", __FUNCTION__);
  if (pp_error != PP_OK) {
    fprintf(stderr, "UrlLoadRequest::FinishStreamingToFileCallback: %d\n", pp_error);
    Fail();
    return;
  }
  PP_Resource fileref = response_interface_->GetBodyAsFileRef(response_);
  if (0 == fileref) {
    fprintf(stderr, "UrlLoadRequest::FinishStreamingToFileCallback: null file");
    Fail();
    return;
  }
  pp_error = fileio_interface_->Open(
      fileio_,
      fileref,
      PP_FILEOPENFLAG_READ,
      PP_MakeCompletionCallback(::OpenFileBodyCallback, this));
  assert(pp_error != PP_OK);  // Open() never succeeds synchronously.
  if (pp_error != PP_OK_COMPLETIONPENDING)  {  // Async failure.
    fprintf(stderr, "PPB_FileIO::Open: %d\n", pp_error);
    Fail();
    return;
  }
}

void OpenRequest::OpenFileBodyCallback(int32_t pp_error) {
  // fprintf(stderr, "%s\n", __FUNCTION__);
  if (pp_error != PP_OK) {
    fprintf(stderr, "UrlLoadRequest::OpenFileBodyCallback: %d\n", pp_error);
    Fail();
    return;
  }
  Success();
}


void OpenRequest::Fail() {
  // fprintf(stderr, "%s\n", __FUNCTION__);
  errno_ = ENOENT;
  fileio = 0;
  // TODO: free fileio
  pthread_mutex_lock(&mu_);
  done_ = true;
  pthread_cond_signal(&cv_);
  pthread_mutex_unlock(&mu_);
}

void OpenRequest::Success() {
  // fprintf(stderr, "%s\n", __FUNCTION__);
  errno_ = 0;
  fileio = fileio_;
  pthread_mutex_lock(&mu_);
  done_ = true;
  pthread_cond_signal(&cv_);
  pthread_mutex_unlock(&mu_);
}

bool OpenRequest::Start() {
  done_ = false;
  // TODO: is it legal to ask for the core interface off the main thread?
  pp::Module::Get()->core()->CallOnMainThread(0, pp::CompletionCallback(::Start_MainThread, this), PP_OK);
  return true;
}

bool OpenRequest::Wait() {
  pthread_mutex_lock(&mu_);
  while(!done_)
    pthread_cond_wait(&cv_, &mu_);
  pthread_mutex_unlock(&mu_);
  return true;
}
