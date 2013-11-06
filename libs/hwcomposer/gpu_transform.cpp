#ifdef USE_GPU_PROCESS_VIDEO
#include <cutils/log.h>
#include <hardware/gralloc.h>
#include "gralloc_priv.h"
#include <ui/GraphicBuffer.h>
#include "scale_rotate.h"

#define GLES2_TRANSFORM 0

#include <EGL/egl.h>
#include <EGL/eglext.h>
#if GLES2_TRANSFORM
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#define glFramebufferTexture2DOES glFramebufferTexture2D
#define glGenFramebuffersOES glGenFramebuffers
#define glBindFramebufferOES glBindFramebuffer
#define GL_FRAMEBUFFER_OES GL_FRAMEBUFFER
#define GL_COLOR_ATTACHMENT0_OES GL_COLOR_ATTACHMENT0
#else
#include <GLES/gl.h>
#include <GLES/glext.h>
#endif

gralloc_module_t const* module;

static EGLDisplay	egl_dpy;
static EGLContext	egl_context;
static EGLSurface	egl_surface;
static unsigned int is_init = 0;
static unsigned int last_transform = 0xffff;

#ifdef _DEBUG
#define GL_CHECK(x) \
    x; \
    { \
	    GLenum err = glGetError(); \
	    if(err != GL_NO_ERROR) { \
		    ALOGE("glGetError() = %i (0x%.8x) at line %i\n", err, err, __LINE__); \
	    } \
    }
#else
#define GL_CHECK(x) x
#endif

static GLuint tex_src;
static GLuint tex_dst;

static GLfloat vertices[] = {
	-1.0f, -1.0f,
	1.0f, -1.0f,
	1.0f,  1.0f,
	-1.0f,  1.0f
};

static GLfloat texcoords[] = {
	0.0f, 0.0f,
	1.0f, 0.0f,
	1.0f, 1.0f,
	0.0f, 1.0f
};

void install()
{
    GL_CHECK(glDisable(GL_CULL_FACE));
    GL_CHECK(glDisable(GL_SCISSOR_TEST));
    GL_CHECK(glDisable(GL_DEPTH_TEST));
    GL_CHECK(glDisable(GL_BLEND));
    GL_CHECK(glDisable(GL_DITHER));

    glGenTextures(1, &tex_src);
    glGenTextures(1, &tex_dst);

    glActiveTexture(GL_TEXTURE0);
    glTexParameterf(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameterf(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameterf(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameterf(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MIN_FILTER, GL_LINEAR);

#if GLES2_TRANSFORM
    const char* vert_src[] =  {
		"\
		attribute highp   vec4 vert_position;\
		attribute mediump vec2 vert_texcoord;\
		varying   mediump vec2 tcoord;\
		void main() \
		{ \
			tcoord = vert_texcoord; \
			gl_Position = vert_position; \
		} \
		"
	};

    const char* frag_src[] = {
		"\
        #extension GL_OES_EGL_image_external:require\n\
		precision mediump float; \
		varying mediump vec2 tcoord; \
		uniform samplerExternalOES tex; \
		void main() \
		{ \
			gl_FragColor = texture2D(tex, tcoord); \
		}",
	};

    GLuint vert_shader;
    GLuint frag_shader;
    GLuint convert_program;
    int status;
    char buffer[1024];

    GL_CHECK(vert_shader = glCreateShader(GL_VERTEX_SHADER));
    GL_CHECK(glShaderSource(vert_shader, 1, vert_src, 0));
    GL_CHECK(glCompileShader(vert_shader));
    GL_CHECK(glGetShaderiv(vert_shader, GL_COMPILE_STATUS, &status));
    GL_CHECK(glGetShaderInfoLog(vert_shader, 1024, NULL, buffer));
    ALOGI("Shader Log: %s\n", buffer);

    GL_CHECK(frag_shader = glCreateShader(GL_FRAGMENT_SHADER));
    GL_CHECK(glShaderSource(frag_shader, 1, frag_src, 0));
    GL_CHECK(glCompileShader(frag_shader));
    GL_CHECK(glGetShaderiv(frag_shader, GL_COMPILE_STATUS, &status));
    GL_CHECK(glGetShaderInfoLog(frag_shader, 1024, NULL, buffer));
    ALOGI("Shader Log: %s\n", buffer);

    GL_CHECK(convert_program = glCreateProgram());
    GL_CHECK(glAttachShader(convert_program, vert_shader));
    GL_CHECK(glAttachShader(convert_program, frag_shader));
    GL_CHECK(glLinkProgram(convert_program));
    GL_CHECK(glGetProgramiv(convert_program, GL_COMPILE_STATUS, &status));
    GL_CHECK(glGetProgramInfoLog(convert_program, 1024, NULL, buffer));
    ALOGI("Program Log: %s\n", buffer);

    GL_CHECK(glUseProgram(convert_program));

    GLuint i_position0;
    GLuint i_texcoord0;
    GL_CHECK(i_position0 = glGetAttribLocation(convert_program, "vert_position"));
    GL_CHECK(i_texcoord0 = glGetAttribLocation(convert_program, "vert_texcoord"));
    GL_CHECK(glEnableVertexAttribArray(i_position0));
    GL_CHECK(glEnableVertexAttribArray(i_texcoord0));
    GL_CHECK(glVertexAttribPointer(i_position0, 2, GL_FLOAT, GL_FALSE, 0, vertices));
    GL_CHECK(glVertexAttribPointer(i_texcoord0, 2, GL_FLOAT, GL_FALSE, 0, texcoords));

    GLuint texture_location;
    GL_CHECK(texture_location = glGetUniformLocation(convert_program, "tex"));
    glUniform1i(texture_location, 0);
#else
    GL_CHECK(glEnable(GL_TEXTURE_EXTERNAL_OES));

    GL_CHECK(glClientActiveTexture(GL_TEXTURE0));
    GL_CHECK(glEnableClientState(GL_VERTEX_ARRAY));
    GL_CHECK(glEnableClientState(GL_TEXTURE_COORD_ARRAY));
    GL_CHECK(glVertexPointer(2, GL_FLOAT, 0, vertices));
    GL_CHECK(glTexCoordPointer(2, GL_FLOAT, 0, texcoords));

    GL_CHECK(glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE));
#endif

    GLuint fbo;
    GL_CHECK(glGenFramebuffersOES(1, &fbo));
    GL_CHECK(glBindFramebufferOES(GL_FRAMEBUFFER_OES, fbo));
}

int init()
{
    int err = hw_get_module(GRALLOC_HARDWARE_MODULE_ID, (const struct hw_module_t**)&module);

    EGLint attribs[] = {
    EGL_BUFFER_SIZE, 16,
    EGL_NONE };
    EGLint context_attribs[] = {
#if GLES2_TRANSFORM
		EGL_CONTEXT_CLIENT_VERSION, 2,
#endif
    EGL_NONE };
    EGLConfig config;
    EGLint num_config;
    EGLBoolean ret;

    egl_dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (egl_dpy == EGL_NO_DISPLAY) {
        ALOGE("eglGetDisplay get default display failed\n");
        goto err_out;
    }

    ret = eglChooseConfig(egl_dpy, attribs, &config, 1, &num_config);
    if (!ret) {
        ALOGE("eglChooseConfig() failed\n");
        goto err_out;
    }

    egl_surface = eglCreatePbufferSurface(egl_dpy, config, NULL);
    if (egl_surface == EGL_NO_SURFACE) {
        ALOGE("gelCreateWindowSurface failed.\n");
        goto err_out;
    }

    egl_context = eglCreateContext(egl_dpy, config, EGL_NO_CONTEXT, context_attribs);
    if (egl_context == EGL_NO_CONTEXT) {
        ALOGE("eglCreateContext failed\n");
        goto err_1;
    }

    ret = eglMakeCurrent(egl_dpy, egl_surface, egl_surface, egl_context);
    if (!ret) {
        ALOGE("eglMakeCurrent() failed\n");
        goto err_2;
    }

	install();

    return 0;

err_2:
    eglDestroyContext(egl_dpy, egl_context);
    egl_context = EGL_NO_CONTEXT;
err_1:
    eglDestroySurface(egl_dpy, egl_surface);
    egl_surface = EGL_NO_SURFACE;
err_out:
    egl_dpy = EGL_NO_DISPLAY;
out:
    return -1;
}

#define ALIGN(value, base) (((value) + ((base) - 1)) & ~((base) - 1))

void get_size_stride(uint32_t width, uint32_t height, uint32_t format, uint32_t &size, uint32_t &stride)
{
	switch (format)
	{
	case HAL_PIXEL_FORMAT_YCbCr_420_SP:
	case HAL_PIXEL_FORMAT_YCrCb_420_SP:
	case HAL_PIXEL_FORMAT_YV12:
		stride = ALIGN(width , 16);
		size = height * (stride + ALIGN(stride/2, 16));
		size = ALIGN(size, 4096);
		break;
	default:
		{
			int bpp = 0;
			switch (format)
			{
			case HAL_PIXEL_FORMAT_RGBA_8888:
			case HAL_PIXEL_FORMAT_RGBX_8888:
			case HAL_PIXEL_FORMAT_BGRA_8888:
				bpp = 4;
				break;
			case HAL_PIXEL_FORMAT_RGB_888:
				bpp = 3;
				break;
			case HAL_PIXEL_FORMAT_RGB_565:
			case HAL_PIXEL_FORMAT_RGBA_5551:
			case HAL_PIXEL_FORMAT_RGBA_4444:
				bpp = 2;
				break;
			default:
				return;
			}
			uint32_t bpr = ALIGN(width*bpp, 8);
			size = bpr * height;
			stride = bpr / bpp;
			size = ALIGN(size, 4096);
		}
	}
}

using namespace android;
#ifdef __cplusplus
extern "C"
{
#endif
int transform_layer(uint32_t srcPhy, uint32_t srcVirt, uint32_t srcFormat, uint32_t transform,
									uint32_t srcWidth, uint32_t srcHeight , uint32_t dstPhy ,
									uint32_t dstVirt, uint32_t dstFormat , uint32_t dstWidth,
									uint32_t dstHeight , struct sprd_rect *trim_rect , uint32_t tmp_phy_addr,
									uint32_t tmp_vir_addr)
{
    EGLDisplay oldDpy  = eglGetCurrentDisplay();
    EGLContext oldCtx  = eglGetCurrentContext();
    EGLSurface oldRead = eglGetCurrentSurface(EGL_READ);
    EGLSurface oldDraw = eglGetCurrentSurface(EGL_DRAW);

    if(!is_init && (init() == -1))
    {
        return -1;
    }

    is_init = 1;

    eglMakeCurrent(egl_dpy, egl_surface, egl_surface, egl_context);

    EGLImageKHR img_src;
    EGLImageKHR img_dst;
    ump_handle ump_h;
    uint32_t size;
    uint32_t stride;

    get_size_stride(srcWidth, srcHeight, srcFormat, size, stride);
    ump_h = ump_handle_create_from_phys_block(srcPhy, size);
    if(ump_h == NULL)
    {
        ALOGE("ump_h_src create fail");
        return -1;
    }
    private_handle_t handle_src(private_handle_t::PRIV_FLAGS_USES_UMP | private_handle_t::PRIV_FLAGS_USES_PHY,
                            size,
                            srcVirt,
                            private_handle_t::LOCK_STATE_MAPPED,
                            ump_secure_id_get(ump_h),
                            ump_h);
    handle_src.width = stride;
    handle_src.height = srcHeight;
    handle_src.format = srcFormat;
    sp<GraphicBuffer> buf_src = new GraphicBuffer(srcWidth, srcHeight, srcFormat,
                                            GraphicBuffer::USAGE_HW_TEXTURE,
                                            stride,
                                            (native_handle_t*)&handle_src, false);
    if(buf_src->initCheck() != NO_ERROR)
    {
        ALOGE("buf_src create fail");
        return -1;
    }

    get_size_stride(dstWidth, dstHeight, dstFormat, size, stride);
    ump_h = ump_handle_create_from_phys_block(dstPhy, size);
    if(ump_h == NULL)
    {
        ALOGE("ump_h_dst create fail");
        return -1;
    }
    private_handle_t handle_dst(private_handle_t::PRIV_FLAGS_USES_UMP | private_handle_t::PRIV_FLAGS_USES_PHY,
                            size,
                            dstVirt,
                            private_handle_t::LOCK_STATE_MAPPED,
                            ump_secure_id_get(ump_h),
                            ump_h);
    handle_dst.width = stride;
    handle_dst.height = dstHeight;
    handle_dst.format = dstFormat;
    sp<GraphicBuffer> buf_dst = new GraphicBuffer(dstWidth, dstHeight, dstFormat,
                                            GraphicBuffer::USAGE_HW_RENDER,
                                            stride,
                                            (native_handle_t*)&handle_dst, false);
    if(buf_dst->initCheck() != NO_ERROR)
    {
        ALOGE("buf_dst create fail");
        return -1;
    }

	if(last_transform != transform)
	{
		last_transform = transform;
#if !GLES2_TRANSFORM
		switch(transform) {
		case 0:
			glMatrixMode(GL_MODELVIEW);
			glLoadIdentity();
			break;
		default:
		case HAL_TRANSFORM_ROT_90:
			glMatrixMode(GL_MODELVIEW);
			glLoadIdentity();
			glRotatef(90.0, 0.0f, 0.0f, 1.0f);
			break;
		case HAL_TRANSFORM_ROT_180:
			glMatrixMode(GL_MODELVIEW);
			glLoadIdentity();
			glRotatef(180.0, 0.0f, 0.0f, 1.0f);
			break;
		case HAL_TRANSFORM_ROT_270:
			glMatrixMode(GL_MODELVIEW);
			glLoadIdentity();
			glRotatef(270.0, 0.0f, 0.0f, 1.0f);
			break;
		case HAL_TRANSFORM_FLIP_H:
			glMatrixMode(GL_MODELVIEW);
			glLoadIdentity();
			glRotatef(180.0, 0.0f, 1.0f, 0.0f);
			break;
		}
#endif
	}

    if(trim_rect)
    {
        texcoords[0] = texcoords[6] = trim_rect->x / (float)srcWidth;
        texcoords[1] = texcoords[3] = trim_rect->y / (float)srcHeight;
        texcoords[2] = texcoords[4] = (trim_rect->x + trim_rect->w) / (float)srcWidth;
        texcoords[5] = texcoords[7] = (trim_rect->y + trim_rect->h) / (float)srcHeight;
    }
    else
    {
        texcoords[0] = texcoords[6] = 0.0f;
        texcoords[1] = texcoords[3] = 0.0f;
        texcoords[2] = texcoords[4] = 1.0f;
        texcoords[5] = texcoords[7] = 1.0f;
    }


    static EGLint attribs[] = {
    EGL_IMAGE_PRESERVED_KHR,    EGL_TRUE,
    EGL_NONE};

    img_src = eglCreateImageKHR(egl_dpy, EGL_NO_CONTEXT, EGL_NATIVE_BUFFER_ANDROID, (EGLClientBuffer)buf_src->getNativeBuffer(), attribs);
    if(img_src == EGL_NO_IMAGE_KHR)
    {
        ALOGE("img_src create fail, error = %x", eglGetError());
        return -1;
    }
    GL_CHECK(glBindTexture(GL_TEXTURE_EXTERNAL_OES, tex_src));
    GL_CHECK(glEGLImageTargetTexture2DOES(GL_TEXTURE_EXTERNAL_OES, (GLeglImageOES)img_src));

    img_dst = eglCreateImageKHR(egl_dpy, EGL_NO_CONTEXT, EGL_NATIVE_BUFFER_ANDROID, (EGLClientBuffer)buf_dst->getNativeBuffer(), attribs);
    if(img_dst == EGL_NO_IMAGE_KHR)
    {
        ALOGE("img_dst create fail, error = %x", eglGetError());
        return -1;
    }
    GL_CHECK(glBindTexture(GL_TEXTURE_2D, tex_dst));
    GL_CHECK(glEGLImageTargetTexture2DOES(GL_TEXTURE_2D, (GLeglImageOES)img_dst));

    GL_CHECK(glFramebufferTexture2DOES(GL_FRAMEBUFFER_OES, GL_COLOR_ATTACHMENT0_OES, GL_TEXTURE_2D, tex_dst, 0));

    GL_CHECK(glViewport(0, 0, dstWidth, dstHeight));

    GL_CHECK(glDrawArrays(GL_TRIANGLE_FAN, 0, 4));

    GL_CHECK(glFinish());

    eglDestroyImageKHR(egl_dpy, img_src);
    eglDestroyImageKHR(egl_dpy, img_dst);

    ump_free_handle_from_mapped_phys_block((ump_handle)handle_src.ump_mem_handle);
    ump_free_handle_from_mapped_phys_block((ump_handle)handle_dst.ump_mem_handle);

    eglMakeCurrent(oldDpy, oldDraw, oldRead, oldCtx);

    return 0;
}
#ifdef __cplusplus
}
#endif

#endif
