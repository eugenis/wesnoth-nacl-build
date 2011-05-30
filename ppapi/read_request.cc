#include "read_request.h"
#include <stdio.h>
#include <errno.h>
#include <ppapi/cpp/completion_callback.h>


static void ReadFileBodyCallback(void* user_data, int32_t pp_error_or_bytes) {
  ReadRequest* obj = reinterpret_cast<ReadRequest*>(user_data);
  if (NULL != obj)
    obj->ReadFileBodyCallback(pp_error_or_bytes);
}

static void Start_MainThread(void* user_data, int32_t pp_error) {
  // fprintf(stderr, "%s\n", __FUNCTION__);
  ReadRequest* obj = reinterpret_cast<ReadRequest*>(user_data);
  if (NULL != obj)
    obj->Start_MainThread();
}


void ReadRequest::Start_MainThread() {
  // fprintf(stderr, "%s\n", __FUNCTION__);

  pp::Module* module = pp::Module::Get();

  fileio_interface_ = static_cast<const PPB_FileIO_Dev*>(module->GetBrowserInterface(PPB_FILEIO_DEV_INTERFACE));

  int32_t pp_error_or_bytes = fileio_interface_->Read(
      fileio,
      offset,
      buf,
      count,
      PP_MakeCompletionCallback(::ReadFileBodyCallback, this));
  if (pp_error_or_bytes >= PP_OK) {  // Synchronous read, callback ignored.
    fprintf(stderr, "Synchronous read\n");
    ReadFileBodyCallback(pp_error_or_bytes);
  } else if (pp_error_or_bytes != PP_OK_COMPLETIONPENDING) {  // Asynch failure.
    fprintf(stderr, "PPB_FILEIO::Read: %d\n", pp_error_or_bytes);
    Fail();
  }
}

void ReadRequest::ReadFileBodyCallback(int32_t pp_error_or_bytes) {
  // fprintf(stderr, "%s\n", __FUNCTION__);
  if (pp_error_or_bytes < PP_OK) {
    fprintf(stderr, "UrlLoadRequest::ReadFileBodyCallback: %d\n", pp_error_or_bytes);
    read_count = 0;
    Fail();
  } else if (pp_error_or_bytes == PP_OK) {  // Reached EOF.
    read_count = 0;
    Success();
  } else {
    read_count = pp_error_or_bytes;
    Success();
  }
}

void ReadRequest::Fail() {
  // fprintf(stderr, "%s\n", __FUNCTION__);
  errno_ = ENOENT;
  // TODO: free fileio
  pthread_mutex_lock(&mu_);
  done_ = true;
  pthread_cond_signal(&cv_);
  pthread_mutex_unlock(&mu_);
}

void ReadRequest::Success() {
  // fprintf(stderr, "%s\n", __FUNCTION__);
  errno_ = 0;
  pthread_mutex_lock(&mu_);
  done_ = true;
  pthread_cond_signal(&cv_);
  pthread_mutex_unlock(&mu_);
}

bool ReadRequest::Start() {
  done_ = false;
  pp::Module::Get()->core()->CallOnMainThread(0, pp::CompletionCallback(::Start_MainThread, this), PP_OK);
  return true;
}

bool ReadRequest::Wait() {
  // fprintf(stderr, "%s\n", __FUNCTION__);
  pthread_mutex_lock(&mu_);
  while(!done_)
    pthread_cond_wait(&cv_, &mu_);
  pthread_mutex_unlock(&mu_);
  return true;
}
