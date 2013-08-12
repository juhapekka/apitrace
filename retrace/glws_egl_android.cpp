#include <dlfcn.h>
#include <gui/SurfaceComposerClient.h>
#include "os.hpp"
#include "glws.hpp"

using namespace android;

namespace glws {

const int  magic_z = 0x7FFFFFFF;

static EGLDisplay eglDisplay = EGL_DEFAULT_DISPLAY;
sp<SurfaceComposerClient> composer_client;

class AndroidEglVisual : public Visual
{
public:
    EGLConfig config;

    AndroidEglVisual(EGLConfig cfg) :
        config (cfg)
    {}
};

class AndroidEglDrawable : public Drawable
{
public:
    sp<SurfaceControl> surface_control;
    ANativeWindow* native_window;
    sp<ANativeWindow> window;
    EGLSurface surface;
    EGLint api;

    AndroidEglDrawable(const Visual *vis, int w, int h, bool pbuffer) :
        Drawable (vis, w, h, pbuffer),
        api(EGL_OPENGL_ES_API)
    {}

    void
    resize(int w, int h) {
        int iRVal;
        
        if (w == width && h == height) {
            return;
        }
        
        eglWaitClient();

        composer_client->openGlobalTransaction();
        iRVal = surface_control->setSize(w, h);
        composer_client->closeGlobalTransaction();
        
        if (iRVal != NO_ERROR) {
            os::log("%s: android::SurfaceControl->setSize return %x "\
                "!= NO_ERROR\n", __FILE__, iRVal);
        }
        Drawable::resize(w, h);
        eglWaitNative(EGL_CORE_NATIVE_ENGINE);
    }

    void
    show(void) {
        int iRVal;
        
        if (visible) {
            return;
        }
        
        composer_client->openGlobalTransaction();
        iRVal = surface_control->show();
        composer_client->closeGlobalTransaction();
        
        if (iRVal != NO_ERROR) {
            os::log("%s: android::SurfaceControl->show return %x "\
                "!= NO_ERROR\n", __FILE__, iRVal);
        }

        eglWaitNative(EGL_CORE_NATIVE_ENGINE);
        Drawable::show();
    }

    void
    swapBuffers(void) {
        eglBindAPI(api);
        eglSwapBuffers(eglDisplay, surface);
    }
};

class AndroidEglContext : public Context
{
public:
    EGLContext context;

    AndroidEglContext(const Visual *vis, Profile prof,
                      EGLContext ctx) :
        Context(vis, prof),
        context(ctx)
    {}

    ~AndroidEglContext() 
    {}
};

/*
 With Android there is not too many events to look for..
 */
bool
processEvents(void) {
    return true;
}

/**
 * Load the symbols from the specified shared object into global namespace, so
 * that they can be later found by dlsym(RTLD_NEXT, ...);
 */
static void
load(const char *filename)
{
    if (!dlopen(filename, RTLD_GLOBAL | RTLD_LAZY)) {
        os::log("%s: unable to open file %s\n", __FILE__, filename);
        os::abort();
    }
}


void
init(void) {
    composer_client = new SurfaceComposerClient;

    if (composer_client->initCheck() != NO_ERROR) {
        composer_client->dispose();
        os::log("%s: composer_client->initCheck() != NO_ERROR\n", __FILE__);
        os::abort();
    }
}

void
cleanup(void) {
//    waffle_display_disconnect(dpy);
}

Visual *
createVisual(bool doubleBuffer, Profile profile) {
    EGLConfig config;
    EGLint num_configs;
    int i;
    // possible combinations
    const EGLint api_bits_gl[7] = {
        EGL_OPENGL_BIT | EGL_OPENGL_ES_BIT | EGL_OPENGL_ES2_BIT,
        EGL_OPENGL_BIT | EGL_OPENGL_ES_BIT,
        EGL_OPENGL_BIT | EGL_OPENGL_ES2_BIT,
        EGL_OPENGL_BIT,
        EGL_OPENGL_ES_BIT | EGL_OPENGL_ES2_BIT,
        EGL_OPENGL_ES2_BIT,
        EGL_OPENGL_ES_BIT,
    };
    const EGLint api_bits_gles1[7] = {
        EGL_OPENGL_BIT | EGL_OPENGL_ES_BIT | EGL_OPENGL_ES2_BIT,
        EGL_OPENGL_ES_BIT | EGL_OPENGL_ES2_BIT,
        EGL_OPENGL_BIT | EGL_OPENGL_ES_BIT,
        EGL_OPENGL_ES_BIT,
        EGL_OPENGL_BIT | EGL_OPENGL_ES2_BIT,
        EGL_OPENGL_BIT,
        EGL_OPENGL_ES2_BIT,
    };
    const EGLint api_bits_gles2[7] = {
        EGL_OPENGL_BIT | EGL_OPENGL_ES_BIT | EGL_OPENGL_ES2_BIT,
        EGL_OPENGL_ES_BIT | EGL_OPENGL_ES2_BIT,
        EGL_OPENGL_BIT | EGL_OPENGL_ES2_BIT,
        EGL_OPENGL_ES2_BIT,
        EGL_OPENGL_BIT | EGL_OPENGL_ES_BIT,
        EGL_OPENGL_BIT,
        EGL_OPENGL_ES_BIT,
    };
    const EGLint *api_bits;

    switch(profile) {
    case PROFILE_COMPAT:
        api_bits = api_bits_gl;
        break;
    case PROFILE_ES1:
        api_bits = api_bits_gles1;
        break;
    case PROFILE_ES2:
        api_bits = api_bits_gles2;
        break;
    default:
        return NULL;
    };

    for (i = 0; i < 7; i++) {
        Attributes<EGLint> attribs;

        attribs.add(EGL_SURFACE_TYPE, EGL_WINDOW_BIT);
        attribs.add(EGL_RED_SIZE, 1);
        attribs.add(EGL_GREEN_SIZE, 1);
        attribs.add(EGL_BLUE_SIZE, 1);
        attribs.add(EGL_ALPHA_SIZE, 1);
        attribs.add(EGL_DEPTH_SIZE, 1);
        attribs.add(EGL_STENCIL_SIZE, 1);
        attribs.add(EGL_RENDERABLE_TYPE, api_bits[i]);
        attribs.end(EGL_NONE);

        if (eglChooseConfig(eglDisplay, attribs, &config, 1, &num_configs) 
            && num_configs == 1 ) {
            
            return new AndroidEglVisual(config);
        }
    }
    os::log("%s: eglChooseConfig(..) no good config found\n", __FILE__);
    return NULL;
}

Drawable *
createDrawable(const Visual *visual, int width, int height, bool pbuffer)
{
    AndroidEglDrawable* localEglDrawable = 
        new AndroidEglDrawable(visual, width, height, pbuffer);
    EGLConfig config = static_cast<const AndroidEglVisual *>(visual)->config;
    EGLint iRVal;
    
    localEglDrawable->surface_control = composer_client->createSurface(
            String8("EGLRetracer Surface"),
            width, height,
            PIXEL_FORMAT_RGB_888, 0);
    
    if (localEglDrawable->surface_control == NULL ||
        localEglDrawable->surface_control->isValid() != true) {
        os::log("%s: Unable to get valid android::SurfaceControl", __FILE__);
        goto error;        
    }

    composer_client->openGlobalTransaction();
    iRVal = localEglDrawable->surface_control->setLayer(magic_z);
    composer_client->closeGlobalTransaction();
    if (iRVal != NO_ERROR) {
        os::log("%s: Unable to get valid android::SurfaceControl", __FILE__);
        goto error;        
    }

    localEglDrawable->window = localEglDrawable->surface_control->getSurface();
    localEglDrawable->native_window = localEglDrawable->window.get();
    
    localEglDrawable->surface = eglCreateWindowSurface(eglDisplay, config, 
        (EGLNativeWindowType)localEglDrawable->native_window, NULL);

    return localEglDrawable;

error:
    delete localEglDrawable;
    return NULL;
}

Context *
createContext(const Visual *_visual, Context *shareContext, Profile profile,
              bool debug)
{
    const AndroidEglVisual *visual = static_cast<const AndroidEglVisual *>(_visual);
    EGLContext share_context = EGL_NO_CONTEXT;
    EGLContext context;
    Attributes<EGLint> attribs;

    if (shareContext) {
        share_context = static_cast<AndroidEglContext*>(shareContext)->context;
    }

    EGLint api = eglQueryAPI();

    switch (profile) {
    case PROFILE_COMPAT:
        load("libGL.so.1");
        eglBindAPI(EGL_OPENGL_API);
        break;
    case PROFILE_CORE:
        assert(0);
        return NULL;
    case PROFILE_ES1:
        load("libGLESv1_CM.so.1");
        eglBindAPI(EGL_OPENGL_ES_API);
        break;
    case PROFILE_ES2:
        load("libGLESv2.so.2");
        eglBindAPI(EGL_OPENGL_ES_API);
        attribs.add(EGL_CONTEXT_CLIENT_VERSION, 2);
        break;
    default:
        return NULL;
    }

    attribs.end(EGL_NONE);

    context = eglCreateContext(eglDisplay, visual->config, share_context, attribs);
    if (!context)
        return NULL;

    eglBindAPI(api);
    return new AndroidEglContext(visual, profile, context);
}

bool
makeCurrent(Drawable *drawable, Context *context)
{
    bool ok;
    
    if (!drawable || !context) {
        return eglMakeCurrent(eglDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);;
    } else {
        AndroidEglDrawable *androidEglDrawable =
            static_cast<AndroidEglDrawable *>(drawable);

        AndroidEglContext *androidEglContext =
            static_cast<AndroidEglContext *>(context);

        ok = eglMakeCurrent(eglDisplay, androidEglDrawable->surface,
                            androidEglDrawable->surface, androidEglContext->context);

        if (ok) {
            EGLint api;

            eglQueryContext(eglDisplay, androidEglContext->context,
                            EGL_CONTEXT_CLIENT_TYPE, &api);

            androidEglDrawable->api = api;
        }
        
        return ok;
    }
}

} /* namespace glws */
