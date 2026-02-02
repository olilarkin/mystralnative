#pragma once

#include <cstdint>
#include <string>
#include <memory>

// Forward declare WebGPU types
typedef struct WGPUDeviceImpl* WGPUDevice;
typedef struct WGPUQueueImpl* WGPUQueue;
typedef struct WGPUInstanceImpl* WGPUInstance;

namespace mystral {
namespace video {

/**
 * Video recording configuration
 */
struct VideoRecorderConfig {
    int fps = 60;                    // Target framerate
    int width = 0;                   // Recording width (0 = use window size)
    int height = 0;                  // Recording height (0 = use window size)
    int quality = 80;                // Encoding quality (0-100 for WebP)
    bool convertToMp4 = false;       // Convert to MP4 after recording
};

/**
 * Video recording statistics
 */
struct VideoRecorderStats {
    int capturedFrames = 0;          // Total frames captured
    int droppedFrames = 0;           // Frames dropped due to backpressure
    int encodedFrames = 0;           // Frames encoded
    double elapsedSeconds = 0.0;     // Recording duration
    double avgFps = 0.0;             // Average capture framerate
};

/**
 * Abstract Video Recorder Interface
 *
 * Provides platform-independent video recording capabilities.
 * Factory method creates the appropriate implementation:
 * - macOS: ScreenCaptureKit (native OS capture)
 * - Windows: Windows.Graphics.Capture (native OS capture)
 * - Linux/Fallback: GPU Readback (WebGPU texture readback + WebP encoding)
 *
 * Usage:
 *   auto recorder = VideoRecorder::create(device, queue, instance);
 *   recorder->startRecording(sdlWindow, "output.mp4", config);
 *   // ... run main loop ...
 *   recorder->stopRecording();
 */
class VideoRecorder {
public:
    virtual ~VideoRecorder() = default;

    /**
     * Start recording from a native window
     * @param nativeWindowHandle Platform-specific window handle:
     *        - macOS: SDL_Window* (used to get NSWindow via SDL3 properties)
     *        - Windows: SDL_Window* (used to get HWND via SDL3 properties)
     *        - Linux: SDL_Window* (used for GPU readback)
     * @param outputPath Path to output video file (MP4 or WebP)
     * @param config Recording configuration
     * @return true on success
     */
    virtual bool startRecording(void* nativeWindowHandle,
                                const std::string& outputPath,
                                const VideoRecorderConfig& config = {}) = 0;

    /**
     * Stop recording and finalize the video file
     * @return true on success
     */
    virtual bool stopRecording() = 0;

    /**
     * Check if currently recording
     * @return true if recording is active
     */
    virtual bool isRecording() const = 0;

    /**
     * Get recording statistics
     */
    virtual VideoRecorderStats getStats() const = 0;

    /**
     * Get the recorder type name (for debugging)
     */
    virtual const char* getTypeName() const = 0;

    /**
     * Process pending capture operations (call once per frame during recording)
     * For GPU readback recorders, this processes async buffer maps.
     * For OS-level capture, this may be a no-op.
     */
    virtual void processFrame() = 0;

    /**
     * Submit a frame for capture (for GPU readback recorders)
     * @param texture WebGPU texture (WGPUTexture) to capture from
     * @param width Texture width
     * @param height Texture height
     * @return true if frame was submitted
     *
     * Note: This is a no-op for native OS capture recorders which capture
     * directly from the window. Only GPU readback recorders use this.
     */
    virtual bool captureFrame(void* texture, uint32_t width, uint32_t height) {
        (void)texture; (void)width; (void)height;
        return true;  // No-op for native capture
    }

    /**
     * Create a video recorder appropriate for the current platform
     * @param device WebGPU device (for GPU fallback recorder)
     * @param queue WebGPU queue (for GPU fallback recorder)
     * @param instance WebGPU instance (for GPU fallback recorder)
     * @return Unique pointer to the recorder, or nullptr on failure
     *
     * The factory method selects the best recorder for the platform:
     * - macOS 12.3+: ScreenCaptureKitRecorder (native capture)
     * - Windows 10 1803+: WindowsGraphicsCaptureRecorder (native capture)
     * - Other/Fallback: GPUReadbackRecorder (WebGPU readback)
     */
    static std::unique_ptr<VideoRecorder> create(
        WGPUDevice device = nullptr,
        WGPUQueue queue = nullptr,
        WGPUInstance instance = nullptr
    );

    /**
     * Check if native OS-level capture is available on this platform
     * @return true if ScreenCaptureKit (macOS) or Windows.Graphics.Capture (Windows) is available
     */
    static bool isNativeCaptureAvailable();

protected:
    VideoRecorder() = default;
};

}  // namespace video
}  // namespace mystral
