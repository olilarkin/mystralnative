/**
 * Windows.Graphics.Capture Implementation Stub
 *
 * TODO: Implement Windows.Graphics.Capture properly.
 *
 * The full implementation requires careful handling of C++/WinRT namespace
 * conflicts. Options include:
 * 1. Use a separate DLL that isolates C++/WinRT code entirely
 * 2. Use COM directly without C++/WinRT projections
 * 3. Use the older DXGI Desktop Duplication API instead
 *
 * For now, this is stubbed out and Windows uses GPU readback fallback (Dawn only).
 */

#ifdef _WIN32

#include <memory>
#include <string>

namespace mystral {
namespace video {

class WindowsGraphicsCaptureRecorderImpl {
public:
    bool startRecording(void* hwnd, const std::string& outputPath, int fps, int width, int height) {
        (void)hwnd; (void)outputPath; (void)fps; (void)width; (void)height;
        return false;
    }
    bool stopRecording() { return false; }
    bool isRecording() const { return false; }
    int getCapturedFrames() const { return 0; }
    int getDroppedFrames() const { return 0; }
};

bool checkWindowsGraphicsCaptureAvailable() {
    // Disabled - C++/WinRT namespace conflicts need resolution
    // GPU readback fallback will be used on Windows with Dawn
    return false;
}

std::unique_ptr<WindowsGraphicsCaptureRecorderImpl> createWindowsGraphicsCaptureImpl() {
    return nullptr;
}

// Interface functions called from windows_graphics_capture.cpp
bool implStartRecording(WindowsGraphicsCaptureRecorderImpl* impl, void* hwnd,
                        const std::string& outputPath, int fps, int width, int height) {
    if (!impl) return false;
    return impl->startRecording(hwnd, outputPath, fps, width, height);
}

bool implStopRecording(WindowsGraphicsCaptureRecorderImpl* impl) {
    if (!impl) return false;
    return impl->stopRecording();
}

bool implIsRecording(WindowsGraphicsCaptureRecorderImpl* impl) {
    if (!impl) return false;
    return impl->isRecording();
}

int implGetCapturedFrames(WindowsGraphicsCaptureRecorderImpl* impl) {
    if (!impl) return 0;
    return impl->getCapturedFrames();
}

int implGetDroppedFrames(WindowsGraphicsCaptureRecorderImpl* impl) {
    if (!impl) return 0;
    return impl->getDroppedFrames();
}

}  // namespace video
}  // namespace mystral

#endif  // _WIN32
