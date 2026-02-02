/**
 * Windows.Graphics.Capture Video Recorder (Windows 10 1803+)
 *
 * Uses Windows.Graphics.Capture API for high-quality, low-overhead screen capture.
 * Captures the SDL window directly and encodes to H.264/MP4 using Media Foundation.
 *
 * Requirements:
 * - Windows 10 version 1803 (April 2018 Update) or later
 * - Graphics Capture capability
 *
 * Architecture:
 * - Uses pimpl pattern to isolate C++/WinRT from Windows.h
 * - WinRT implementation is in windows_graphics_capture_impl.cpp
 */

#include "mystral/video/video_recorder.h"

#ifdef _WIN32

#include <iostream>
#include <memory>

namespace mystral {
namespace video {

// Forward declaration of the implementation class (defined in windows_graphics_capture_impl.cpp)
class WindowsGraphicsCaptureRecorderImpl;

// Check if Windows.Graphics.Capture is available (Windows 10 1803+)
// Implemented in windows_graphics_capture_impl.cpp
extern bool checkWindowsGraphicsCaptureAvailable();

// Create the implementation (implemented in windows_graphics_capture_impl.cpp)
extern std::unique_ptr<WindowsGraphicsCaptureRecorderImpl> createWindowsGraphicsCaptureImpl();

// Implementation interface functions (defined in windows_graphics_capture_impl.cpp)
extern bool implStartRecording(WindowsGraphicsCaptureRecorderImpl* impl, void* hwnd,
                                const std::string& outputPath, int fps, int width, int height);
extern bool implStopRecording(WindowsGraphicsCaptureRecorderImpl* impl);
extern bool implIsRecording(WindowsGraphicsCaptureRecorderImpl* impl);
extern int implGetCapturedFrames(WindowsGraphicsCaptureRecorderImpl* impl);
extern int implGetDroppedFrames(WindowsGraphicsCaptureRecorderImpl* impl);

/**
 * Windows Graphics Capture Video Recorder
 *
 * Wrapper that delegates to the pimpl implementation to avoid
 * C++/WinRT namespace conflicts with Windows.h
 */
class WindowsGraphicsCaptureRecorder : public VideoRecorder {
public:
    WindowsGraphicsCaptureRecorder();
    ~WindowsGraphicsCaptureRecorder() override;

    bool startRecording(void* nativeWindowHandle,
                        const std::string& outputPath,
                        const VideoRecorderConfig& config) override;
    bool stopRecording() override;
    bool isRecording() const override;
    VideoRecorderStats getStats() const override;
    const char* getTypeName() const override { return "WindowsGraphicsCaptureRecorder"; }
    void processFrame() override;
    bool captureFrame(void* texture, uint32_t width, uint32_t height) override;

private:
    friend std::unique_ptr<VideoRecorder> createWindowsGraphicsCaptureRecorder();
    std::unique_ptr<WindowsGraphicsCaptureRecorderImpl> impl_;
};

WindowsGraphicsCaptureRecorder::WindowsGraphicsCaptureRecorder() {
    impl_ = createWindowsGraphicsCaptureImpl();
}

WindowsGraphicsCaptureRecorder::~WindowsGraphicsCaptureRecorder() = default;

bool WindowsGraphicsCaptureRecorder::startRecording(void* nativeWindowHandle,
                                                      const std::string& outputPath,
                                                      const VideoRecorderConfig& config) {
    if (!impl_) {
        std::cerr << "[WindowsGraphicsCapture] Implementation not available" << std::endl;
        return false;
    }
    return implStartRecording(impl_.get(), nativeWindowHandle, outputPath,
                              config.fps, config.width, config.height);
}

bool WindowsGraphicsCaptureRecorder::stopRecording() {
    if (!impl_) return false;
    return implStopRecording(impl_.get());
}

bool WindowsGraphicsCaptureRecorder::isRecording() const {
    if (!impl_) return false;
    return implIsRecording(impl_.get());
}

VideoRecorderStats WindowsGraphicsCaptureRecorder::getStats() const {
    VideoRecorderStats stats{};
    if (impl_) {
        stats.capturedFrames = implGetCapturedFrames(impl_.get());
        stats.droppedFrames = implGetDroppedFrames(impl_.get());
    }
    return stats;
}

void WindowsGraphicsCaptureRecorder::processFrame() {
    // No-op - capture happens via WinRT callbacks
}

bool WindowsGraphicsCaptureRecorder::captureFrame(void* texture, uint32_t width, uint32_t height) {
    (void)texture; (void)width; (void)height;
    return true; // No-op - capture happens via WinRT callbacks
}

// Factory function
std::unique_ptr<VideoRecorder> createWindowsGraphicsCaptureRecorder() {
    if (!checkWindowsGraphicsCaptureAvailable()) {
        return nullptr;
    }
    auto recorder = std::make_unique<WindowsGraphicsCaptureRecorder>();
    if (!recorder->impl_) {
        return nullptr;
    }
    return recorder;
}

bool isWindowsGraphicsCaptureAvailableCheck() {
    return checkWindowsGraphicsCaptureAvailable();
}

}  // namespace video
}  // namespace mystral

#else  // Not Windows

namespace mystral {
namespace video {

std::unique_ptr<VideoRecorder> createWindowsGraphicsCaptureRecorder() {
    return nullptr;
}

bool isWindowsGraphicsCaptureAvailableCheck() {
    return false;
}

}  // namespace video
}  // namespace mystral

#endif  // _WIN32
