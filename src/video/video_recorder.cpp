/**
 * Video Recorder Factory Implementation
 *
 * Creates the appropriate video recorder for the current platform:
 * - macOS 12.3+: ScreenCaptureKitRecorder (native OS capture)
 * - Windows 10 1803+: WindowsGraphicsCaptureRecorder (native OS capture)
 * - Linux/Fallback: GPUReadbackRecorder (WebGPU texture readback)
 */

#include "mystral/video/video_recorder.h"
#include <iostream>

#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

namespace mystral {
namespace video {

// Forward declarations of factory functions from platform-specific files
std::unique_ptr<VideoRecorder> createScreenCaptureKitRecorder();
std::unique_ptr<VideoRecorder> createWindowsGraphicsCaptureRecorder();
std::unique_ptr<VideoRecorder> createGPUReadbackRecorder(
    WGPUDevice device, WGPUQueue queue, WGPUInstance instance);

// Forward declarations of availability check functions
bool isScreenCaptureKitAvailableCheck();
bool isWindowsGraphicsCaptureAvailableCheck();

// Factory method implementation
std::unique_ptr<VideoRecorder> VideoRecorder::create(
    WGPUDevice device,
    WGPUQueue queue,
    WGPUInstance instance
) {
#if defined(__APPLE__) && TARGET_OS_MAC && !TARGET_OS_IPHONE
    // macOS: Try ScreenCaptureKit first
    if (isScreenCaptureKitAvailableCheck()) {
        auto recorder = createScreenCaptureKitRecorder();
        if (recorder) {
            std::cout << "[VideoRecorder] Using ScreenCaptureKit (native macOS capture)" << std::endl;
            return recorder;
        }
    }
#endif

#ifdef _WIN32
    // Windows: Try Windows.Graphics.Capture first
    if (isWindowsGraphicsCaptureAvailableCheck()) {
        auto recorder = createWindowsGraphicsCaptureRecorder();
        if (recorder) {
            std::cout << "[VideoRecorder] Using Windows.Graphics.Capture (native Windows capture)" << std::endl;
            return recorder;
        }
    }
#endif

    // Fallback: GPU Readback recorder (works on all platforms with WebGPU)
    if (device && queue && instance) {
        std::cout << "[VideoRecorder] Using GPU Readback recorder (WebGPU fallback)" << std::endl;
        return createGPUReadbackRecorder(device, queue, instance);
    }

    std::cerr << "[VideoRecorder] No suitable recorder available" << std::endl;
    return nullptr;
}

// Check if native capture is available
bool VideoRecorder::isNativeCaptureAvailable() {
#if defined(__APPLE__) && TARGET_OS_MAC && !TARGET_OS_IPHONE
    if (isScreenCaptureKitAvailableCheck()) {
        return true;
    }
#endif

#ifdef _WIN32
    if (isWindowsGraphicsCaptureAvailableCheck()) {
        return true;
    }
#endif

    return false;
}

}  // namespace video
}  // namespace mystral
