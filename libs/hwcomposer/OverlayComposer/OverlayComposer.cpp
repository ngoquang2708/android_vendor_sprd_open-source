/*
 * Copyright (C) 2010 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */


/******************************************************************************
 **                   Edit    History                                         *
 **---------------------------------------------------------------------------*
 ** DATE          Module              DESCRIPTION                             *
 ** 16/08/2013    Hardware Composer   Add a new feature to Harware composer,  *
 **                                   verlayComposer use GPU to do the        *
 **                                   Hardware layer blending on Overlay      *
 **                                   buffer, and then post the OVerlay       *
 **                                   buffer to Display                       *
 ******************************************************************************
 ** Author:         zhongjun.chen@spreadtrum.com                              *
 *****************************************************************************/

#include "OverlayComposer.h"
#include "GLErro.h"
#include "Layer.h"


namespace android
{


OverlayComposer::OverlayComposer(overlayDevice_t *dev)
    : mDev(dev), InitFlag(0), mNumLayer(0),
      mDisplay(EGL_NO_DISPLAY), mSurface(EGL_NO_SURFACE),
      mContext(EGL_NO_CONTEXT),
      mConfig(0), mWidth(0), mHeight(0), /*mFormat(0),*/
      mFlags(0)
{
};


OverlayComposer::~OverlayComposer()
{
    deInitOpenGLES();
    deInitEGL();
    sem_destroy(&cmdSem);
    sem_destroy(&doneSem);
    sem_destroy(&displaySem);
}

void OverlayComposer::onFirstRef()
{
    run("OverlayComposer", PRIORITY_URGENT_DISPLAY + PRIORITY_MORE_FAVORABLE);
}

status_t OverlayComposer::readyToRun()
{
    static bool initGFXFlag = false;

    if (initGFXFlag == false)
    {
        bool ret = initEGL();
        if (!ret)
        {
            ALOGE("Init EGL ENV failed");
            return -1;
        }

        initOpenGLES();

        initGFXFlag = true;
    }

    sem_init(&cmdSem, 0, 0);
    sem_init(&doneSem, 0, 0);
    sem_init(&displaySem, 0, 0);

    return NO_ERROR;
}

bool OverlayComposer::threadLoop()
{

    sem_wait(&cmdSem);

    composerHWLayers();

    glFinish();

    sem_post(&doneSem);

    /* *******************************
     * waiting display
     * *******************************/
    sem_wait(&displaySem);
    swapBuffers();

    return true;
}


status_t OverlayComposer::selectConfigForPixelFormat(
        EGLDisplay dpy,
        EGLint const* attrs,
        PixelFormat format,
        EGLConfig* outConfig)
{
    EGLConfig config = NULL;
    EGLint numConfigs = -1, n=0;
    eglGetConfigs(dpy, NULL, 0, &numConfigs);
    EGLConfig* const configs = new EGLConfig[numConfigs];
    eglChooseConfig(dpy, attrs, configs, numConfigs, &n);
    for (int i=0 ; i<n ; i++) {
        EGLint nativeVisualId = 0;
        eglGetConfigAttrib(dpy, configs[i], EGL_NATIVE_VISUAL_ID, &nativeVisualId);
        if (nativeVisualId>0 && format == nativeVisualId) {
            *outConfig = configs[i];
            delete [] configs;
            return NO_ERROR;
        }
    }
    delete [] configs;
    return NAME_NOT_FOUND;
}

bool OverlayComposer::initEGL()
{

    mWindow = new OverlayNativeWindow(mDev);
    if (mWindow == NULL)
    {
        ALOGE("Create Native Window failed, NO mem");
        return false;
    }

    if (!(mWindow->Init()))
    {
        ALOGE("Init Native Window failed");
        return false;
    }

    int format;
    ANativeWindow const * const window = mWindow.get();
    window->query(window, NATIVE_WINDOW_FORMAT, &format);

    EGLint w, h, dummy;
    EGLint numConfigs=0;
    EGLSurface surface;
    EGLContext context;
    EGLBoolean result;
    status_t err;

    // initialize EGL
    EGLint attribs[] = {
            EGL_SURFACE_TYPE,       EGL_WINDOW_BIT,
            EGL_NONE,               0,
            EGL_NONE
    };


    EGLDisplay display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    checkEGLErrors("eglGetDisplay");
    eglInitialize(display, NULL, NULL);
    eglGetConfigs(display, NULL, 0, &numConfigs);

    EGLConfig config = NULL;
    err = selectConfigForPixelFormat(display, attribs, format, &config);
    ALOGE_IF(err, "couldn't find an EGLConfig matching the screen format");

    EGLint r,g,b,a;
    eglGetConfigAttrib(display, config, EGL_RED_SIZE,   &r);
    eglGetConfigAttrib(display, config, EGL_GREEN_SIZE, &g);
    eglGetConfigAttrib(display, config, EGL_BLUE_SIZE,  &b);
    eglGetConfigAttrib(display, config, EGL_ALPHA_SIZE, &a);

    //if (window->isUpdateOnDemand()) {
    //    mFlags |= PARTIAL_UPDATES;
    //}

    //if (eglGetConfigAttrib(display, config, EGL_CONFIG_CAVEAT, &dummy) == EGL_TRUE) {
    //   if (dummy == EGL_SLOW_CONFIG)
    //       mFlags |= SLOW_CONFIG;
    //}


     /*
     * Create our main surface
     */

    surface = eglCreateWindowSurface(display, config, mWindow.get(), NULL);
    checkEGLErrors("eglCreateWindowSurface");
    eglQuerySurface(display, surface, EGL_WIDTH,  &mWidth);
    eglQuerySurface(display, surface, EGL_HEIGHT, &mHeight);

    //if (mFlags & PARTIAL_UPDATES) {
    //    // if we have partial updates, we definitely don't need to
    //    // preserve the backbuffer, which may be costly.
    //    eglSurfaceAttrib(display, surface,
    //            EGL_SWAP_BEHAVIOR, EGL_BUFFER_DESTROYED);
    //}

    /*
     * Create our OpenGL ES context
     */
//#define EGL_IMG_context_priority
//#define HAS_CONTEXT_PRIORITY
    EGLint contextAttributes[] = {
#ifdef EGL_IMG_context_priority
#ifdef HAS_CONTEXT_PRIORITY
#warning "using EGL_IMG_context_priority"
        EGL_CONTEXT_PRIORITY_LEVEL_IMG, EGL_CONTEXT_PRIORITY_HIGH_IMG,
#endif
#endif
        EGL_NONE, EGL_NONE
    };
    context = eglCreateContext(display, config, NULL, contextAttributes);
    checkEGLErrors("eglCreateContext");
    mDisplay = display;
    mConfig  = config;
    mSurface = surface;
    mContext = context;
    //mFormat  = ;
    //mPageFlipCount = 0;

    /*
     * Gather OpenGL ES extensions
     */

    result = eglMakeCurrent(display, surface, surface, context);
    if (!result) {
        ALOGE("Couldn't create a working GLES context. check logs. exiting...");
        return false;
    }
    checkEGLErrors("eglMakeCurrent");

    //GLExtensions& extensions(GLExtensions::getInstance());
    //extensions.initWithGLStrings(
    //        glGetString(GL_VENDOR),
    //        glGetString(GL_RENDERER),
    //        glGetString(GL_VERSION),
    //        glGetString(GL_EXTENSIONS),
    //        eglQueryString(display, EGL_VENDOR),
    //        eglQueryString(display, EGL_VERSION),
    //        eglQueryString(display, EGL_EXTENSIONS));

    //glGetIntegerv(GL_MAX_TEXTURE_SIZE, &mMaxTextureSize);
    //glGetIntegerv(GL_MAX_VIEWPORT_DIMS, mMaxViewportDims);

    //ALOGI("EGL informations:");
    //ALOGI("# of configs : %d", numConfigs);
    //ALOGI("vendor    : %s", extensions.getEglVendor());
    //ALOGI("version   : %s", extensions.getEglVersion());
    //ALOGI("extensions: %s", extensions.getEglExtension());
    //ALOGI("Client API: %s", eglQueryString(display, EGL_CLIENT_APIS)?:"Not Supported");
    //ALOGI("EGLSurface: %d-%d-%d-%d, config=%p", r, g, b, a, config);

    //ALOGI("OpenGL informations:");
    //ALOGI("vendor    : %s", extensions.getVendor());
    //ALOGI("renderer  : %s", extensions.getRenderer());
    //ALOGI("version   : %s", extensions.getVersion());
    //ALOGI("extensions: %s", extensions.getExtension());
    //ALOGI("GL_MAX_TEXTURE_SIZE = %d", mMaxTextureSize);
    //ALOGI("GL_MAX_VIEWPORT_DIMS = %d x %d", mMaxViewportDims[0], mMaxViewportDims[1]);
    //ALOGI("flags = %08x", mFlags);

    // Unbind the context from this thread
    //eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);


    return true;
}

void OverlayComposer::deInitEGL()
{
    eglMakeCurrent(mDisplay, EGL_NO_SURFACE,
                   EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroyContext(mDisplay, mContext);
    eglDestroySurface(mDisplay, mSurface);
    eglTerminate(mDisplay);
    mDisplay = EGL_NO_DISPLAY;
}

bool OverlayComposer::initOpenGLES()
{
    // Initialize OpenGL|ES
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glPixelStorei(GL_PACK_ALIGNMENT, 4);
    glEnableClientState(GL_VERTEX_ARRAY);
    glShadeModel(GL_FLAT);
    glDisable(GL_DITHER);
    glDisable(GL_CULL_FACE);

    const uint16_t g0 = pack565(0x0F,0x1F,0x0F);
    const uint16_t g1 = pack565(0x17,0x2f,0x17);
    const uint16_t wormholeTexData[4] = { g0, g1, g1, g0 };
    glGenTextures(1, &mWormholeTexName);
    glBindTexture(GL_TEXTURE_2D, mWormholeTexName);
    glTexParameterx(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameterx(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameterx(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameterx(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 2, 2, 0,
            GL_RGB, GL_UNSIGNED_SHORT_5_6_5, wormholeTexData);

    const uint16_t protTexData[] = { pack565(0x03, 0x03, 0x03) };
    glGenTextures(1, &mProtectedTexName);
    glBindTexture(GL_TEXTURE_2D, mProtectedTexName);
    glTexParameterx(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameterx(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameterx(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameterx(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 1, 1, 0,
            GL_RGB, GL_UNSIGNED_SHORT_5_6_5, protTexData);

    glViewport(0, 0, mDev->fb_width, mDev->fb_height);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    // put the origin in the left-bottom corner
    glOrthof(0, mDev->fb_width, 0, mDev->fb_height, 0, 1);
    // l=0, r=w ; b=0, t=h

    return true;
}

void OverlayComposer::deInitOpenGLES()
{
    glDeleteTextures(1, &mWormholeTexName);
    glDeleteTextures(1, &mProtectedTexName);
}

void OverlayComposer::caculateLayerRect(hwc_layer_t  *l, struct LayerRect *rect, struct LayerRect *rV)
{
    if (l == NULL || rect == NULL)
    {
        ALOGE("overlayDevice::caculateLayerRect, input parameters is NULL");
        return;
    }

    const native_handle_t *pNativeHandle = l->handle;
    struct private_handle_t *private_h = (struct private_handle_t *)pNativeHandle;

    if (private_h == NULL)
    {
        ALOGE("overlayDevice::caculateLayerRect, buffer handle is NULL");
        return;
    }

    if (private_h->format == HAL_PIXEL_FORMAT_RGBA_8888 ||
        private_h->format == HAL_PIXEL_FORMAT_RGBX_8888 ||
        private_h->format == HAL_PIXEL_FORMAT_RGB_565)
    {
        rect->left = MAX(l->sourceCrop.left, 0);
        rect->left = MIN(rect->left, private_h->width);
        rect->top = MAX(l->sourceCrop.top, 0);
        rect->top = MIN(rect->top, private_h->height);
        rect->right = MIN(l->sourceCrop.right, private_h->width);
        rect->bottom = MIN(l->sourceCrop.bottom, private_h->height);
    }
    else if (private_h->format == HAL_PIXEL_FORMAT_YCbCr_420_SP ||
             private_h->format == HAL_PIXEL_FORMAT_YCrCb_420_SP ||
             private_h->format != HAL_PIXEL_FORMAT_YV12)
    {
        rect->left = MAX(l->sourceCrop.left, 0);
        rect->top = MAX(l->sourceCrop.top, 0);
        rect->right = MIN(l->sourceCrop.right, private_h->width);
        rect->bottom = MIN(l->sourceCrop.bottom, private_h->height);
    }

    rV->left = l->displayFrame.left;
    rV->top = l->displayFrame.top;
    rV->right = l->displayFrame.right;
    rV->bottom = l->displayFrame.bottom;
}


int OverlayComposer::composerHWLayers()
{
    int status = -1;
    uint32_t numLayer = 0;

    if (mList == NULL)
    {
        ALOGE("The HWC List is NULL");
        return status;
    }

    numLayer = mList->numHwLayers;
    if (numLayer <= 0)
    {
        ALOGE("Cannot find HWC layers");
        return status;
    }


    for (unsigned int i = 0; i < mList->numHwLayers; i++)
    {
        hwc_layer_t  *pL = &(mList->hwLayers[i]);
        if (pL == NULL)
        {
            numLayer--;
            //ALOGE("Find %dth layer is NULL", i);
            continue;
        }

        if (pL->compositionType != HWC_OVERLAY)
        {
            numLayer--;
            continue;
        }

        struct private_handle_t *pH = (struct private_handle_t *)pL->handle;
        if (pH == NULL)
        {
            //ALOGD("%dth Layer handle is NULL", i);
            numLayer--;
            continue;
        }

        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();

        Layer *L = new Layer(pH, mDisplay, mDev->fb_width, mDev->fb_height);
        if (L == NULL)
        {
            ALOGE("The %dth Layer object is NULL", numLayer);
            return status;
        }

        struct LayerRect r;
        struct LayerRect rV;

        memset(&r, 0, sizeof(struct LayerRect));
        caculateLayerRect(pL, &r, &rV);

        L->setLayerTransform(pL->transform);
        L->setLayerRect(&r, &rV);
        L->setLayerAlpha(pL->alpha);

        L->draw();


        /*
         * Store the Layer object to a list
         **/
        mDrawLayerList.push_back(L);
    }


    status = 0;

    return status;
}

bool OverlayComposer::onComposer(hwc_layer_list_t* l)
{
    if (l == NULL)
    {
        ALOGE("hwc_layer_list is NULL");
        return false;
    }

    mList = l;

    /*
     *  Send signal to composer thread to start
     *  composer work.
     * */
    sem_post(&cmdSem);

    /*
     *  Waiting composer work done.
     * */
    sem_wait(&doneSem);

    return true;
}

void OverlayComposer::onDisplay()
{
    sem_post(&displaySem);
}

bool OverlayComposer::swapBuffers()
{
    eglSwapBuffers(mDisplay, mSurface);

    /* delete some layer object from list
     * Now, these object are useless
     * */
    for (DrawLayerList::iterator it = mDrawLayerList.begin();
         it != mDrawLayerList.end(); it++)
    {
        Layer *mL = *it;
        delete mL;
        mL = NULL;
        //mDrawLayerList.erase(it);
    }
    mDrawLayerList.clear();

    return true;
}


};

