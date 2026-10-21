#include "mod/MotionBlur.h"

#include <algorithm>
#include <dlfcn.h>

namespace camera_overhaul {
namespace {

static const char *kVertexShader =
    "attribute vec2 aPos;\n"
    "attribute vec2 aTex;\n"
    "varying vec2 vTex;\n"
    "void main() {\n"
    "    vTex = aTex;\n"
    "    gl_Position = vec4(aPos, 0.0, 1.0);\n"
    "}\n";

static const char *kFragmentShader =
    "precision mediump float;\n"
    "varying vec2 vTex;\n"
    "uniform sampler2D uTex;\n"
    "uniform float uAlpha;\n"
    "void main() {\n"
    "    vec4 col = texture2D(uTex, vTex);\n"
    "    gl_FragColor = vec4(col.rgb, uAlpha);\n"
    "}\n";

static constexpr GLfloat kQuadVertices[] = {
    // aPos.x, aPos.y, aTex.x, aTex.y
    -1.0f, -1.0f, 0.0f, 0.0f,
     1.0f, -1.0f, 1.0f, 0.0f,
    -1.0f,  1.0f, 0.0f, 1.0f,
     1.0f,  1.0f, 1.0f, 1.0f,
};

static GLuint compileShader(GLenum type, const char *src) {
    GLuint shader = glCreateShader(type);
    if (!shader) {
        return 0;
    }
    glShaderSource(shader, 1, &src, nullptr);
    glCompileShader(shader);
    GLint compiled = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (!compiled) {
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

} // namespace

MotionBlur &MotionBlur::instance() {
    static MotionBlur instance;
    return instance;
}

MotionBlur::~MotionBlur() {
    removeHook();
    cleanupGL();
}

void MotionBlur::setEnabled(bool enabled) {
    mEnabled.store(enabled, std::memory_order_relaxed);
    if (!enabled) {
        std::lock_guard lock(mRenderMutex);
        mFirstFrame = true;
    }
}

bool MotionBlur::isEnabled() const {
    return mEnabled.load(std::memory_order_relaxed);
}

void MotionBlur::setIntensity(float intensity) {
    mIntensity.store(std::clamp(intensity, 0.05f, 0.90f), std::memory_order_relaxed);
}

float MotionBlur::getIntensity() const {
    return mIntensity.load(std::memory_order_relaxed);
}

bool MotionBlur::installHook() {
    if (mSwapHook.installed()) {
        return true;
    }

    void *target = nullptr;
    void *eglLib = dlopen("libEGL.so", RTLD_NOW | RTLD_GLOBAL);
    if (eglLib) {
        target = dlsym(eglLib, "eglSwapBuffers");
    }
    if (!target) {
        target = dlsym(RTLD_DEFAULT, "eglSwapBuffers");
    }
    if (!target) {
        return false;
    }

    void *original = nullptr;
    pl::memory::HookHandle handle(
        target,
        reinterpret_cast<void *>(&MotionBlur::swapBuffersDetour),
        &original);

    if (!handle.installed()) {
        return false;
    }

    mOriginalSwapBuffers = reinterpret_cast<EglSwapBuffersFn>(original);
    mSwapHook = std::move(handle);
    return true;
}

void MotionBlur::removeHook() {
    mSwapHook.reset();
    mOriginalSwapBuffers = nullptr;
    cleanupGL();
}

EGLBoolean MotionBlur::swapBuffersDetour(EGLDisplay dpy, EGLSurface surf) {
    auto &mb = MotionBlur::instance();
    if (mb.isEnabled()) {
        mb.onSwapBuffers(dpy, surf);
    }
    if (mb.mOriginalSwapBuffers) {
        return mb.mOriginalSwapBuffers(dpy, surf);
    }
    return EGL_TRUE;
}

bool MotionBlur::initGL(GLint width, GLint height) {
    cleanupGL();

    GLuint vs = compileShader(GL_VERTEX_SHADER, kVertexShader);
    GLuint fs = compileShader(GL_FRAGMENT_SHADER, kFragmentShader);
    if (!vs || !fs) {
        if (vs) glDeleteShader(vs);
        if (fs) glDeleteShader(fs);
        return false;
    }

    mProgram = glCreateProgram();
    glAttachShader(mProgram, vs);
    glAttachShader(mProgram, fs);
    glLinkProgram(mProgram);

    glDeleteShader(vs);
    glDeleteShader(fs);

    GLint linked = 0;
    glGetProgramiv(mProgram, GL_LINK_STATUS, &linked);
    if (!linked) {
        glDeleteProgram(mProgram);
        mProgram = 0;
        return false;
    }

    mPosAttr = glGetAttribLocation(mProgram, "aPos");
    mTexAttr = glGetAttribLocation(mProgram, "aTex");
    mAlphaUniform = glGetUniformLocation(mProgram, "uAlpha");
    mTexUniform = glGetUniformLocation(mProgram, "uTex");

    glGenTextures(1, &mAccumTexture);
    glBindTexture(GL_TEXTURE_2D, mAccumTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    mWidth = width;
    mHeight = height;
    mFirstFrame = true;
    return true;
}

void MotionBlur::cleanupGL() {
    if (mAccumTexture) {
        glDeleteTextures(1, &mAccumTexture);
        mAccumTexture = 0;
    }
    if (mProgram) {
        glDeleteProgram(mProgram);
        mProgram = 0;
    }
    mWidth = 0;
    mHeight = 0;
    mFirstFrame = true;
}

void MotionBlur::onSwapBuffers(EGLDisplay dpy, EGLSurface surf) {
    EGLint width = 0, height = 0;
    if (!eglQuerySurface(dpy, surf, EGL_WIDTH, &width) ||
        !eglQuerySurface(dpy, surf, EGL_HEIGHT, &height) ||
        width <= 0 || height <= 0) {
        return;
    }

    const float intensity = mIntensity.load(std::memory_order_relaxed);
    if (intensity <= 0.01f) {
        return;
    }

    std::lock_guard lock(mRenderMutex);

    if (!mProgram || mWidth != width || mHeight != height) {
        if (!initGL(width, height)) {
            return;
        }
    }

    // Save active OpenGL ES state
    GLint prevProgram = 0;
    glGetIntegerv(GL_CURRENT_PROGRAM, &prevProgram);

    GLint prevActiveTex = 0;
    glGetIntegerv(GL_ACTIVE_TEXTURE, &prevActiveTex);

    GLint prevTex = 0;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &prevTex);

    GLint prevArrayBuffer = 0;
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &prevArrayBuffer);

    GLboolean prevBlend = glIsEnabled(GL_BLEND);
    GLboolean prevDepth = glIsEnabled(GL_DEPTH_TEST);
    GLboolean prevCull = glIsEnabled(GL_CULL_FACE);
    GLboolean prevScissor = glIsEnabled(GL_SCISSOR_TEST);

    GLint prevBlendSrc = 0, prevBlendDst = 0;
    glGetIntegerv(GL_BLEND_SRC_RGB, &prevBlendSrc);
    glGetIntegerv(GL_BLEND_DST_RGB, &prevBlendDst);

    GLint prevViewport[4];
    glGetIntegerv(GL_VIEWPORT, prevViewport);

    renderBlurPass(width, height, intensity);

    // Restore original OpenGL ES state
    glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);

    if (prevBlend) glEnable(GL_BLEND); else glDisable(GL_BLEND);
    glBlendFunc(prevBlendSrc, prevBlendDst);

    if (prevDepth) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
    if (prevCull) glEnable(GL_CULL_FACE); else glDisable(GL_CULL_FACE);
    if (prevScissor) glEnable(GL_SCISSOR_TEST); else glDisable(GL_SCISSOR_TEST);

    glUseProgram(prevProgram);
    glActiveTexture(prevActiveTex);
    glBindTexture(GL_TEXTURE_2D, prevTex);
    glBindBuffer(GL_ARRAY_BUFFER, prevArrayBuffer);
}

void MotionBlur::renderBlurPass(GLint width, GLint height, float alpha) {
    if (mFirstFrame) {
        glBindTexture(GL_TEXTURE_2D, mAccumTexture);
        glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, width, height);
        mFirstFrame = false;
        return;
    }

    glViewport(0, 0, width, height);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_SCISSOR_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glUseProgram(mProgram);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, mAccumTexture);
    glUniform1i(mTexUniform, 0);
    glUniform1f(mAlphaUniform, std::clamp(alpha, 0.05f, 0.90f));

    glVertexAttribPointer(mPosAttr, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat), kQuadVertices);
    glEnableVertexAttribArray(mPosAttr);

    glVertexAttribPointer(mTexAttr, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat), kQuadVertices + 2);
    glEnableVertexAttribArray(mTexAttr);

    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    glDisableVertexAttribArray(mPosAttr);
    glDisableVertexAttribArray(mTexAttr);

    // Copy blended result into accumulation texture for the next frame
    glBindTexture(GL_TEXTURE_2D, mAccumTexture);
    glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, width, height);
}

} // namespace camera_overhaul
