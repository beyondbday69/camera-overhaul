#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>

#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <pl/memory/Hook.hpp>

namespace camera_overhaul {

/**
 * @brief Pure native GPU motion blur engine for Minecraft Bedrock on Android.
 *
 * Hooks eglSwapBuffers and renders a high-performance recursive accumulation
 * pass using OpenGL ES 2.0/3.0. Zero CPU overhead, fully GPU-accelerated.
 */
class MotionBlur {
  public:
    static MotionBlur &instance();

    MotionBlur() = default;
    ~MotionBlur();

    void setEnabled(bool enabled);
    [[nodiscard]] bool isEnabled() const;

    void setIntensity(float intensity);
    [[nodiscard]] float getIntensity() const;

    bool installHook();
    void removeHook();

    void onSwapBuffers(EGLDisplay dpy, EGLSurface surf);

  private:
    using EglSwapBuffersFn = EGLBoolean (*)(EGLDisplay dpy, EGLSurface surf);

    static EGLBoolean swapBuffersDetour(EGLDisplay dpy, EGLSurface surf);

    std::atomic<bool> mEnabled{false};
    std::atomic<float> mIntensity{0.55f};

    pl::memory::HookHandle mSwapHook;
    EglSwapBuffersFn mOriginalSwapBuffers{nullptr};

    std::mutex mRenderMutex;
    GLuint mProgram{0};
    GLuint mAccumTexture{0};
    GLint mWidth{0};
    GLint mHeight{0};
    GLint mPosAttr{-1};
    GLint mTexAttr{-1};
    GLint mAlphaUniform{-1};
    GLint mTexUniform{-1};
    bool mFirstFrame{true};

    bool initGL(GLint width, GLint height);
    void cleanupGL();
    void renderBlurPass(GLint width, GLint height, float alpha);
};

} // namespace camera_overhaul
