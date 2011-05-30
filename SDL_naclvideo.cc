#include "SDL_config.h"

#include <assert.h>

#include "SDL_naclvideo.h"
#include "SDL_naclevents_c.h"

#include <ppapi/cpp/instance.h>
#include <ppapi/cpp/graphics_2d.h>
#include <ppapi/cpp/completion_callback.h>
#include <ppapi/cpp/image_data.h>
#include <ppapi/cpp/rect.h>
#include <ppapi/c/pp_errors.h>

pp::Instance* gNaclPPInstance;
static int gNaclVideoWidth;
static int gNaclVideoHeight;

static int kNaClFlushDelayMs = 20;

extern "C" {

#include "SDL_video.h"
#include "SDL_mouse.h"
#include "SDL_nacl.h"
#include "../SDL_sysvideo.h"
#include "../SDL_pixels_c.h"
#include "../../events/SDL_events_c.h"

#define NACLVID_DRIVER_NAME "nacl"

void SDL_NACL_SetInstance(PP_Instance instance, int width, int height) {
  gNaclPPInstance = pp::Module::Get()->InstanceForPPInstance(instance);
  gNaclVideoWidth = width;
  gNaclVideoHeight = height;
}

static void flush(void* data, int32_t unused);

/* Initialization/Query functions */
static int NACL_VideoInit(_THIS, SDL_PixelFormat *vformat);
static SDL_Rect **NACL_ListModes(_THIS, SDL_PixelFormat *format, Uint32 flags);
static SDL_Surface *NACL_SetVideoMode(_THIS, SDL_Surface *current, int width, int height, int bpp, Uint32 flags);
static void NACL_VideoQuit(_THIS);
static void NACL_UpdateRects(_THIS, int numrects, SDL_Rect *rects);

/* The implementation dependent data for the window manager cursor */
struct WMcursor {
  // Fake cursor data to fool SDL into not using its broken (as it seems) software cursor emulation.
};

static void NACL_FreeWMCursor(_THIS, WMcursor *cursor);
static WMcursor *NACL_CreateWMCursor(_THIS,
                Uint8 *data, Uint8 *mask, int w, int h, int hot_x, int hot_y);
static int NACL_ShowWMCursor(_THIS, WMcursor *cursor);
static void NACL_WarpWMCursor(_THIS, Uint16 x, Uint16 y);


static int NACL_Available(void) {
  return gNaclPPInstance != 0;
}

static void NACL_DeleteDevice(SDL_VideoDevice *device) {
  SDL_free(device->hidden);
  SDL_free(device);
}

static SDL_VideoDevice *NACL_CreateDevice(int devindex) {
  SDL_VideoDevice *device;

  assert(gNaclPPInstance);

  /* Initialize all variables that we clean on shutdown */
  device = (SDL_VideoDevice *)SDL_malloc(sizeof(SDL_VideoDevice));
  if ( device ) {
    SDL_memset(device, 0, (sizeof *device));
    device->hidden = (struct SDL_PrivateVideoData *)
        SDL_malloc((sizeof *device->hidden));
  }
  if ( (device == NULL) || (device->hidden == NULL) ) {
    SDL_OutOfMemory();
    if ( device ) {
      SDL_free(device);
    }
    return(0);
  }
  SDL_memset(device->hidden, 0, (sizeof *device->hidden));

  device->hidden->flush_mutex = SDL_CreateMutex();
  device->hidden->flush_cond = SDL_CreateCond();

  device->hidden->flush_pending = false;
  device->hidden->data_ready = false;

  device->hidden->ow = gNaclVideoWidth;
  device->hidden->oh = gNaclVideoHeight;

  if (device->hidden->context2d)
    delete device->hidden->context2d;
  device->hidden->context2d = new pp::Graphics2D(gNaclPPInstance,
      pp::Size(device->hidden->ow, device->hidden->oh), false);
  assert(device->hidden->context2d != NULL);

  if (!gNaclPPInstance->BindGraphics(*device->hidden->context2d)) {
    fprintf(stderr, "***** Couldn't bind the device context *****\n");
  }

  // TODO: convert normal RGBA to premultiplied alpha.
  device->hidden->image_data = new pp::ImageData(gNaclPPInstance,
      PP_IMAGEDATAFORMAT_BGRA_PREMUL,
      device->hidden->context2d->size(),
      false);
  assert(device->hidden->image_data != NULL);

  /* Set the function pointers */
  device->VideoInit = NACL_VideoInit;
  device->ListModes = NACL_ListModes;
  device->SetVideoMode = NACL_SetVideoMode;
  device->UpdateRects = NACL_UpdateRects;
  device->VideoQuit = NACL_VideoQuit;
  device->InitOSKeymap = NACL_InitOSKeymap;
  device->PumpEvents = NACL_PumpEvents;

  device->FreeWMCursor = NACL_FreeWMCursor;
  device->CreateWMCursor = NACL_CreateWMCursor;
  device->ShowWMCursor = NACL_ShowWMCursor;
  device->WarpWMCursor = NACL_WarpWMCursor;

  device->free = NACL_DeleteDevice;

  flush(device, 0);

  return device;
}

VideoBootStrap NACL_bootstrap = {
  NACLVID_DRIVER_NAME, "SDL Native Client video driver",
  NACL_Available, NACL_CreateDevice
};


int NACL_VideoInit(_THIS, SDL_PixelFormat *vformat) {
  fprintf(stderr, "CONGRATULATIONS: You are using the SDL nacl video driver!\n");

  /* Determine the screen depth (use default 8-bit depth) */
  /* we change this during the SDL_SetVideoMode implementation... */
  vformat->BitsPerPixel = 32;
  vformat->BytesPerPixel = 4;

  _this->info.current_w = gNaclVideoWidth;
  _this->info.current_h = gNaclVideoHeight;

  /* We're done! */
  return(0);
}

SDL_Rect **NACL_ListModes(_THIS, SDL_PixelFormat *format, Uint32 flags) {
  return (SDL_Rect **) -1;
}


SDL_Surface *NACL_SetVideoMode(_THIS, SDL_Surface *current,
    int width, int height, int bpp, Uint32 flags) {
  if ( _this->hidden->buffer ) {
    SDL_free( _this->hidden->buffer );
  }

  bpp = 32; // Let SDL handle pixel format conversion.
  width = _this->hidden->ow;
  height = _this->hidden->oh;

  _this->hidden->buffer = SDL_malloc(width * height * (bpp / 8));
  if ( ! _this->hidden->buffer ) {
    SDL_SetError("Couldn't allocate buffer for requested mode");
    return(NULL);
  }

  SDL_memset(_this->hidden->buffer, 0, width * height * (bpp / 8));

  /* Allocate the new pixel format for the screen */
  if ( ! SDL_ReallocFormat(current, bpp, 0xFF0000, 0xFF00, 0xFF, 0) ) {
    SDL_free(_this->hidden->buffer);
    _this->hidden->buffer = NULL;
    SDL_SetError("Couldn't allocate new pixel format for requested mode");
    return(NULL);
  }

  /* Set up the new mode framebuffer */
  current->flags = flags & SDL_FULLSCREEN;
  _this->hidden->bpp = bpp;
  _this->hidden->w = current->w = width;
  _this->hidden->h = current->h = height;
  _this->hidden->pitch = current->pitch = current->w * (bpp / 8);
  current->pixels = _this->hidden->buffer;

  /* We're done */
  return(current);
}

// Called from the browser when the 2D graphics have been flushed out to the device.
static void flush_callback(void* data, int32_t unused) {
  SDL_VideoDevice* _this = reinterpret_cast<SDL_VideoDevice*>(data);

  SDL_LockMutex(_this->hidden->flush_mutex);


  assert(_this->hidden->flush_pending == true);
  // image_data was locked since the last flush.
  assert(_this->hidden->data_ready == false);
  _this->hidden->flush_pending = false;

  // Now is a good time to update image_data.
  SDL_CondSignal(_this->hidden->flush_cond);

  // Do another flush after a small delay.
  pp::Module::Get()->core()->CallOnMainThread(kNaClFlushDelayMs, pp::CompletionCallback(&flush, _this), PP_OK);

  SDL_UnlockMutex(_this->hidden->flush_mutex);
}

// Called periodically on the main thread.
static void flush(void* data, int32_t unused) {
  SDL_VideoDevice* _this = reinterpret_cast<SDL_VideoDevice*>(data);

  SDL_LockMutex(_this->hidden->flush_mutex);

  assert(_this->hidden->flush_pending == false);
  if (_this->hidden->data_ready) {
    // Flush image_data.
    _this->hidden->flush_pending = true;
    _this->hidden->data_ready = false;

    for (int i = 0; i < _this->hidden->updated_rects.size(); ++i) {
      SDL_Rect& r = _this->hidden->updated_rects[i];
      _this->hidden->context2d->PaintImageData(*_this->hidden->image_data, pp::Point(), pp::Rect(r.x, r.y, r.w, r.h));
    }
    _this->hidden->updated_rects.clear();
      
    // _this->hidden->context2d->PaintImageData(*_this->hidden->image_data, pp::Point());

    _this->hidden->context2d->Flush(pp::CompletionCallback(&flush_callback, _this));
  } else {
    // Check again after a small delay.
    pp::Module::Get()->core()->CallOnMainThread(kNaClFlushDelayMs, pp::CompletionCallback(&flush, _this), PP_OK);
  }
  SDL_UnlockMutex(_this->hidden->flush_mutex);
}

static void NACL_UpdateRects(_THIS, int numrects, SDL_Rect *rects) {
  if (_this->hidden->bpp == 0) // not initialized yet
    return;

  assert(_this->hidden->image_data);
  assert(_this->hidden->w == _this->hidden->ow);
  assert(_this->hidden->h == _this->hidden->oh);

  SDL_LockMutex(_this->hidden->flush_mutex);

  // If we are flushing, wait.
  // TODO(eugenis): this blocks the game thread. Should we just drop the frame
  // instead, or store it in some kind of a side buffer?
  while (_this->hidden->flush_pending)
    SDL_CondWait(_this->hidden->flush_cond, _this->hidden->flush_mutex);

  // data_ready may be true here if the previous frame is not flushed yet.
  // No problem, just drop it.

  // Copy the updated areas to the ImageData.
  unsigned pitch = _this->hidden->w * _this->hidden->bpp / 8;
  for (int i = 0; i < numrects; ++i) {
    unsigned start_offset = rects[i].x * _this->hidden->bpp / 8 + rects[i].y * pitch;
    unsigned char* src = (unsigned char*)_this->hidden->buffer + start_offset;
    unsigned char* src_end = src + rects[i].h * pitch;
    unsigned char* dst = (unsigned char*)_this->hidden->image_data->data() + start_offset;
    unsigned line_size = rects[i].w * _this->hidden->bpp / 8;
    while (src < src_end) {
      SDL_memcpy(dst, src, line_size);
      src += pitch;
      dst += pitch;
    }
    _this->hidden->updated_rects.push_back(rects[i]);
  }

  // Clear alpha channel in the ImageData.
  unsigned char* start = (unsigned char*)_this->hidden->image_data->data();
  unsigned char* end = start + (_this->hidden->w * _this->hidden->h * _this->hidden->bpp / 8);
  for (unsigned char* p = start + 3; p < end; p +=4)
    *p = 0xFF;

  _this->hidden->data_ready = true;

  SDL_UnlockMutex(_this->hidden->flush_mutex);

  // if (numrects >= 1)
  //   fprintf(stderr, "UpdateRects(%d), (%d +%d, %d +%d)\n", numrects, rects[0].x, rects[0].w, rects[0].y, rects[0].h);
  // else
  //   fprintf(stderr, "========= UpdateRects(0)\n");
}

static void NACL_FreeWMCursor(_THIS, WMcursor *cursor) {
  delete cursor;
}

static WMcursor *NACL_CreateWMCursor(_THIS,
    Uint8 *data, Uint8 *mask, int w, int h, int hot_x, int hot_y) {
  return new WMcursor();
}

static int NACL_ShowWMCursor(_THIS, WMcursor *cursor) {
  return 1; // Success!
}

static void NACL_WarpWMCursor(_THIS, Uint16 x, Uint16 y) {
}

/* Note:  If we are terminated, this could be called in the middle of
   another SDL video routine -- notably UpdateRects.
*/
void NACL_VideoQuit(_THIS) {
  if (_this->screen->pixels != NULL)
  {
    SDL_free(_this->screen->pixels);
    _this->screen->pixels = NULL;
  }
  delete _this->hidden->context2d;
  delete _this->hidden->image_data;

  SDL_DestroyMutex(_this->hidden->flush_mutex);
  SDL_DestroyCond(_this->hidden->flush_cond);
}
} // extern "C"
