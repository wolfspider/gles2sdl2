

#include <stdexcept>
#include <iostream>
#include <memory>
#include <sstream>
#include <SDL_syswm.h>
#include <SDL_assert.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>

#include "SDL.h"

#define WINDOW_WIDTH    640
#define WINDOW_HEIGHT   480

typedef struct
{
    GLint width;
    GLint height;

    EGLNativeWindowType hWnd;
    EGLDisplay eglDisplay;

    EGLContext eglContext;
    EGLSurface eglSurface;

} ESContext;

EGLBoolean CreateEGLContext ( EGLNativeWindowType hWnd, EGLDisplay* eglDisplay,
                              EGLContext* eglContext, EGLSurface* eglSurface,
                              EGLint* configAttribList, EGLint* surfaceAttribList)
{
   EGLint numConfigs;
   EGLint majorVersion;
   EGLint minorVersion;
   EGLDisplay display;
   EGLContext context;
   EGLSurface surface;
   EGLConfig config;
   EGLint contextAttribs[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE, EGL_NONE };

   // Get Display
   display = eglGetDisplay(GetDC(hWnd)); // EGL_DEFAULT_DISPLAY
   if ( display == EGL_NO_DISPLAY )
   {
      return EGL_FALSE;
   }

   // Initialize EGL
   if ( !eglInitialize(display, &majorVersion, &minorVersion) )
   {
      return EGL_FALSE;
   }

   // Get configs
   if ( !eglGetConfigs(display, NULL, 0, &numConfigs) )
   {
      return EGL_FALSE;
   }

   // Choose config
   if ( !eglChooseConfig(display, configAttribList, &config, 1, &numConfigs) )
   {
      return EGL_FALSE;
   }

   // Create a surface
   surface = eglCreateWindowSurface(display, config, (EGLNativeWindowType)hWnd, surfaceAttribList);
   if ( surface == EGL_NO_SURFACE )
   {
      return EGL_FALSE;
   }

   // Create a GL context
   context = eglCreateContext(display, config, EGL_NO_CONTEXT, contextAttribs );
   if ( context == EGL_NO_CONTEXT )
   {
      return EGL_FALSE;
   }

   // Make the context current
   if ( !eglMakeCurrent(display, surface, surface, context) )
   {
      return EGL_FALSE;
   }

   *eglDisplay = display;
   *eglSurface = surface;
   *eglContext = context;
   return EGL_TRUE;
}

static ESContext* es_context = nullptr;

static const char gVertexShader[] =
    "attribute vec4 vPosition;\n"
    "void main() {\n"
    "  gl_Position = vPosition;\n"
    "}\n";

static const char gFragmentShader[] =
    "precision mediump float;\n"
    "void main() {\n"
    "  gl_FragColor = vec4(0.0, 1.0, 0.0, 1.0);\n"
    "}\n";

const GLfloat gTriangleVertices[] = { 0.0f, 0.5f, -0.5f, -0.5f,
        0.5f, -0.5f };

/* Call this instead of exit(), so we can clean up SDL: atexit() is evil. */
static void
quit(int rc)
{
    exit(rc);
}

static void printGLString(const char *name, GLenum s) {
    const char *v = (const char *) glGetString(s);
    //LOGI("GL %s = %s\n", name, v);
}

static void checkGlError(const char* op) {
    for (GLint error = glGetError(); error; error
            = glGetError()) {
        //LOGI("after %s() glError (0x%x)\n", op, error);
    }
}

GLuint loadShader(GLenum shaderType, const char* pSource) {
    GLuint shader = glCreateShader(shaderType);
    if (shader) {
        glShaderSource(shader, 1, &pSource, NULL);
        glCompileShader(shader);
        GLint compiled = 0;
        glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
        if (!compiled) {
            GLint infoLen = 0;
            glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &infoLen);
            if (infoLen) {
                char* buf = (char*) malloc(infoLen);
                if (buf) {
                    glGetShaderInfoLog(shader, infoLen, NULL, buf);
                    //LOGE("Could not compile shader %d:\n%s\n",
                            //shaderType, buf);
                    free(buf);
                }
                glDeleteShader(shader);
                shader = 0;
            }
        }
    }
    return shader;
}

GLuint createProgram(const char* pVertexSource, const char* pFragmentSource) {
    GLuint vertexShader = loadShader(GL_VERTEX_SHADER, pVertexSource);
    if (!vertexShader) {
        return 0;
    }

    GLuint pixelShader = loadShader(GL_FRAGMENT_SHADER, pFragmentSource);
    if (!pixelShader) {
        return 0;
    }

    GLuint program = glCreateProgram();
    if (program) {
        glAttachShader(program, vertexShader);
        checkGlError("glAttachShader");
        glAttachShader(program, pixelShader);
        checkGlError("glAttachShader");
        glLinkProgram(program);
        GLint linkStatus = GL_FALSE;
        glGetProgramiv(program, GL_LINK_STATUS, &linkStatus);
        if (linkStatus != GL_TRUE) {
            GLint bufLength = 0;
            glGetProgramiv(program, GL_INFO_LOG_LENGTH, &bufLength);
            if (bufLength) {
                char* buf = (char*) malloc(bufLength);
                if (buf) {
                    glGetProgramInfoLog(program, bufLength, NULL, buf);
                    //LOGE("Could not link program:\n%s\n", buf);
                    free(buf);
                }
            }
            glDeleteProgram(program);
            program = 0;
        }
    }
    return program;
}

GLuint gProgram;
GLuint gvPositionHandle;

bool setupGraphics(int w, int h) {
    printGLString("Version", GL_VERSION);
    printGLString("Vendor", GL_VENDOR);
    printGLString("Renderer", GL_RENDERER);
    printGLString("Extensions", GL_EXTENSIONS);

    //LOGI("setupGraphics(%d, %d)", w, h);
    gProgram = createProgram(gVertexShader, gFragmentShader);
    if (!gProgram) {
        //LOGE("Could not create program.");
        return false;
    }
    gvPositionHandle = glGetAttribLocation(gProgram, "vPosition");
    checkGlError("glGetAttribLocation");
    //LOGI("glGetAttribLocation(\"vPosition\") = %d\n",
            //gvPositionHandle);

    glViewport(0, 0, w, h);
    checkGlError("glViewport");
    return true;
}

void renderFrame() {
    static float grey;
    grey += 0.01f;
    if (grey > 1.0f) {
        grey = 0.0f;
    }

    glClearColor(grey, grey, grey, 1.0f);
    checkGlError("glClearColor");
    glClear( GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT);
    checkGlError("glClear");

    glUseProgram(gProgram);
    checkGlError("glUseProgram");

    glVertexAttribPointer(gvPositionHandle, 2, GL_FLOAT, GL_FALSE, 0, gTriangleVertices);
    checkGlError("glVertexAttribPointer");
    glEnableVertexAttribArray(gvPositionHandle);
    checkGlError("glEnableVertexAttribArray");
    glDrawArrays(GL_TRIANGLES, 0, 3);
    checkGlError("glDrawArrays");
}

int main(int argc, char *argv[])
{
    SDL_Window *window;
    int done;
    SDL_Event event;

    window = SDL_CreateWindow("SDL2 OpenGL ES 2",
                              SDL_WINDOWPOS_UNDEFINED,
                              SDL_WINDOWPOS_UNDEFINED,
                              WINDOW_WIDTH, WINDOW_HEIGHT,
                              SDL_WINDOW_SHOWN );

    SDL_SysWMinfo info;
    SDL_VERSION(&info.version);
    SDL_bool get_win_info = SDL_GetWindowWMInfo(window, &info);
    SDL_assert_release(get_win_info);
    EGLNativeWindowType hWnd = info.info.win.window;

    SDL_assert_release(es_context == nullptr);
    es_context = new ESContext();

    es_context->width = WINDOW_WIDTH;
	es_context->height = WINDOW_HEIGHT;
	es_context->hWnd = hWnd;

	EGLint configAttribList[] =
	{
		EGL_RED_SIZE,       8,
		EGL_GREEN_SIZE,     8,
		EGL_BLUE_SIZE,      8,
		EGL_ALPHA_SIZE,     8 /*: EGL_DONT_CARE*/,
		EGL_DEPTH_SIZE,     EGL_DONT_CARE,
		EGL_STENCIL_SIZE,   EGL_DONT_CARE,
		EGL_SAMPLE_BUFFERS, 0,
		EGL_NONE
	};
	EGLint surfaceAttribList[] =
	{
		//EGL_POST_SUB_BUFFER_SUPPORTED_NV, flags & (ES_WINDOW_POST_SUB_BUFFER_SUPPORTED) ? EGL_TRUE : EGL_FALSE,
		EGL_POST_SUB_BUFFER_SUPPORTED_NV, EGL_FALSE,
		EGL_NONE, EGL_NONE
	};

	if ( es_context == nullptr )
	{
		throw std::runtime_error("can't create opengl es 2.0 context, NULL es_context");
	}

	EGLBoolean is_context_created = CreateEGLContext(es_context->hWnd,
			&es_context->eglDisplay,
			&es_context->eglContext,
			&es_context->eglSurface,
			configAttribList,
			surfaceAttribList);

	if (is_context_created == 0)
	{
		throw std::runtime_error("can't create opengl es 2.0 context");
	}

	setupGraphics(WINDOW_WIDTH, WINDOW_HEIGHT);

	/* Main render loop */
    done = 0;
    while (!done) {
        /* Check for events */

        renderFrame();

        eglSwapBuffers( es_context->eglDisplay, es_context->eglSurface );

        while (SDL_PollEvent(&event)) {

            if (event.type == SDL_QUIT || event.type == SDL_KEYDOWN) {
                done = 1;
            }

        }
    }

    if (es_context)
	{
		if (EGL_NO_CONTEXT != es_context->eglContext)
		{
			eglDestroyContext(es_context->eglDisplay, es_context->eglContext);
			es_context->eglContext = EGL_NO_CONTEXT;
		}

		if (EGL_NO_SURFACE != es_context->eglSurface)
		{
			eglDestroySurface(es_context->eglDisplay, es_context->eglSurface);
			es_context->eglSurface = EGL_NO_SURFACE;
		}

		if (EGL_NO_DISPLAY != es_context->eglDisplay)
		{
			eglTerminate(es_context->eglDisplay);
			es_context->eglDisplay = EGL_NO_DISPLAY;
		}

		delete es_context;
		es_context = nullptr;
	}

	quit(0);

}

/* vi: set ts=4 sw=4 expandtab: */
