#include "close_request.h"
#include <stdio.h>
#include <errno.h>
#include <ppapi/cpp/completion_callback.h>


static void Start_MainThread(void* user_data, int32_t pp_error) {
  // fprintf(stderr, "%s\n", __FUNCTION__);
  CloseRequest* obj = reinterpret_cast<CloseRequest*>(user_data);
  if (NULL != obj)
    obj->Start_MainThread();
}

void CloseRequest::Start_MainThread() {
  // fprintf(stderr, "%s\n", __FUNCTION__);

  pp::Module* module = pp::Module::Get();
  fileio_interface_ = static_cast<const PPB_FileIO_Dev*>(module->GetBrowserInterface(PPB_FILEIO_DEV_INTERFACE));

  fileio_interface_->Close(fileio);
  Success();
}

void CloseRequest::Fail() {
  // fprintf(stderr, "%s\n", __FUNCTION__);
  errno_ = ENOENT;
  // TODO: free fileio
  pthread_mutex_lock(&mu_);
  done_ = true;
  pthread_cond_signal(&cv_);
  pthread_mutex_unlock(&mu_);
}

void CloseRequest::Success() {
  // fprintf(stderr, "%s\n", __FUNCTION__);
  errno_ = 0;
  pthread_mutex_lock(&mu_);
  done_ = true;
  pthread_cond_signal(&cv_);
  pthread_mutex_unlock(&mu_);
}

bool CloseRequest::Start() {
  done_ = false;
  pp::Module::Get()->core()->CallOnMainThread(0, pp::CompletionCallback(::Start_MainThread, this), PP_OK);
  return true;
}

bool CloseRequest::Wait() {
  // fprintf(stderr, "%s\n", __FUNCTION__);
  pthread_mutex_lock(&mu_);
  while(!done_)
    pthread_cond_wait(&cv_, &mu_);
  pthread_mutex_unlock(&mu_);
  return true;
}
