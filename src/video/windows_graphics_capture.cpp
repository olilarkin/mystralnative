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
 * - GraphicsCaptureItem: Represents the window to capture
 * - Direct3D11CaptureFramePool: Provides frames with D3D11 textures
 * - Media Foundation SinkWriter: Encodes to H.264 MP4
 */

#include "mystral/video/video_recorder.h"

#ifdef _WIN32

#include <Windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <SDL3/SDL.h>

// Windows.Graphics.Capture requires C++/WinRT
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>
#include <windows.graphics.capture.interop.h>
#include <Windows.Graphics.DirectX.Direct3D11.interop.h>

#include <atomic>
#include <chrono>
#include <iostream>
#include <mutex>
#include <wrl/client.h>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "windowsapp.lib")

using namespace winrt;
using namespace Windows::Foundation;
using namespace Windows::Graphics::Capture;
using namespace Windows::Graphics::DirectX;
using namespace Windows::Graphics::DirectX::Direct3D11;
using Microsoft::WRL::ComPtr;

namespace mystral {
namespace video {

// Check if Windows.Graphics.Capture is available
static bool isWindowsGraphicsCaptureAvailable() {
    // Check Windows version (requires Windows 10 1803+)
    OSVERSIONINFOEXW osvi = { sizeof(osvi), 10, 0, 17134 };  // Build 17134 = 1803
    DWORDLONG conditionMask = 0;
    VER_SET_CONDITION(conditionMask, VER_MAJORVERSION, VER_GREATER_EQUAL);
    VER_SET_CONDITION(conditionMask, VER_MINORVERSION, VER_GREATER_EQUAL);
    VER_SET_CONDITION(conditionMask, VER_BUILDNUMBER, VER_GREATER_EQUAL);

    if (!VerifyVersionInfoW(&osvi, VER_MAJORVERSION | VER_MINORVERSION | VER_BUILDNUMBER, conditionMask)) {
        return false;
    }

    // Check if GraphicsCaptureSession is supported
    return GraphicsCaptureSession::IsSupported();
}

/**
 * Windows Graphics Capture Video Recorder Implementation
 */
class WindowsGraphicsCaptureRecorder : public VideoRecorder {
public:
    WindowsGraphicsCaptureRecorder()
        : recording_(false), frameCount_(0), droppedFrames_(0) {
        // Initialize COM and WinRT
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
    }

    ~WindowsGraphicsCaptureRecorder() override {
        if (isRecording()) {
            stopRecording();
        }
    }

    bool startRecording(void* nativeWindowHandle,
                        const std::string& outputPath,
                        const VideoRecorderConfig& config) override {
        if (recording_) {
            std::cerr << "[WinGraphicsCapture] Already recording" << std::endl;
            return false;
        }

        // Get SDL window
        SDL_Window* sdlWindow = static_cast<SDL_Window*>(nativeWindowHandle);
        if (!sdlWindow) {
            std::cerr << "[WinGraphicsCapture] Invalid window handle" << std::endl;
            return false;
        }

        // Get HWND from SDL
        SDL_PropertiesID props = SDL_GetWindowProperties(sdlWindow);
        HWND hwnd = static_cast<HWND>(SDL_GetPointerProperty(props,
            SDL_PROP_WINDOW_WIN32_HWND_POINTER, NULL));
        if (!hwnd) {
            std::cerr << "[WinGraphicsCapture] Failed to get HWND from SDL" << std::endl;
            return false;
        }

        // Store config
        config_ = config;
        outputPath_ = outputPath;
        frameCount_ = 0;
        droppedFrames_ = 0;

        // Get window size
        RECT rect;
        GetClientRect(hwnd, &rect);
        width_ = config.width > 0 ? config.width : (rect.right - rect.left);
        height_ = config.height > 0 ? config.height : (rect.bottom - rect.top);

        // Create D3D11 device
        UINT createFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
        D3D_FEATURE_LEVEL featureLevel;
        HRESULT hr = D3D11CreateDevice(
            nullptr,
            D3D_DRIVER_TYPE_HARDWARE,
            nullptr,
            createFlags,
            nullptr, 0,
            D3D11_SDK_VERSION,
            &d3dDevice_,
            &featureLevel,
            &d3dContext_
        );

        if (FAILED(hr)) {
            std::cerr << "[WinGraphicsCapture] Failed to create D3D11 device: " << std::hex << hr << std::endl;
            return false;
        }

        // Get DXGI device
        ComPtr<IDXGIDevice> dxgiDevice;
        hr = d3dDevice_.As(&dxgiDevice);
        if (FAILED(hr)) {
            std::cerr << "[WinGraphicsCapture] Failed to get DXGI device" << std::endl;
            return false;
        }

        // Create WinRT Direct3D device
        winrt::com_ptr<::IInspectable> inspectable;
        hr = CreateDirect3D11DeviceFromDXGIDevice(dxgiDevice.Get(), inspectable.put());
        if (FAILED(hr)) {
            std::cerr << "[WinGraphicsCapture] Failed to create WinRT D3D device" << std::endl;
            return false;
        }

        winrtDevice_ = inspectable.as<IDirect3DDevice>();

        // Create capture item from HWND
        auto interopFactory = winrt::get_activation_factory<GraphicsCaptureItem, IGraphicsCaptureItemInterop>();
        GraphicsCaptureItem captureItem{ nullptr };
        hr = interopFactory->CreateForWindow(
            hwnd,
            winrt::guid_of<ABI::Windows::Graphics::Capture::IGraphicsCaptureItem>(),
            reinterpret_cast<void**>(winrt::put_abi(captureItem))
        );

        if (FAILED(hr) || !captureItem) {
            std::cerr << "[WinGraphicsCapture] Failed to create capture item: " << std::hex << hr << std::endl;
            return false;
        }

        captureItem_ = captureItem;

        // Create frame pool
        auto size = captureItem.Size();
        framePool_ = Direct3D11CaptureFramePool::CreateFreeThreaded(
            winrtDevice_,
            DirectXPixelFormat::B8G8R8A8UIntNormalized,
            2,  // Number of buffers
            size
        );

        // Set up frame arrived handler
        framePool_.FrameArrived([this](auto& pool, auto&) {
            handleFrameArrived(pool);
        });

        // Initialize Media Foundation
        hr = MFStartup(MF_VERSION);
        if (FAILED(hr)) {
            std::cerr << "[WinGraphicsCapture] Failed to start Media Foundation" << std::endl;
            return false;
        }
        mfInitialized_ = true;

        // Create sink writer for MP4 output
        if (!createSinkWriter()) {
            return false;
        }

        // Create capture session
        session_ = framePool_.CreateCaptureSession(captureItem_);

        // Start capture
        session_.StartCapture();

        startTime_ = std::chrono::high_resolution_clock::now();
        recording_ = true;

        std::cout << "[WinGraphicsCapture] Started recording " << width_ << "x" << height_
                  << " @ " << config.fps << "fps to " << outputPath << std::endl;

        return true;
    }

    bool stopRecording() override {
        if (!recording_) {
            return false;
        }

        recording_ = false;

        // Stop capture
        if (session_) {
            session_.Close();
            session_ = nullptr;
        }

        if (framePool_) {
            framePool_.Close();
            framePool_ = nullptr;
        }

        // Finalize video
        if (sinkWriter_) {
            HRESULT hr = sinkWriter_->Finalize();
            if (FAILED(hr)) {
                std::cerr << "[WinGraphicsCapture] Failed to finalize video: " << std::hex << hr << std::endl;
            }
            sinkWriter_.Reset();
        }

        // Cleanup
        if (mfInitialized_) {
            MFShutdown();
            mfInitialized_ = false;
        }

        captureItem_ = nullptr;
        winrtDevice_ = nullptr;
        d3dContext_.Reset();
        d3dDevice_.Reset();
        stagingTexture_.Reset();

        std::cout << "[WinGraphicsCapture] Stopped recording. Captured " << frameCount_
                  << " frames, dropped " << droppedFrames_ << std::endl;

        return true;
    }

    bool isRecording() const override {
        return recording_;
    }

    VideoRecorderStats getStats() const override {
        VideoRecorderStats stats;
        stats.capturedFrames = frameCount_;
        stats.droppedFrames = droppedFrames_;
        stats.encodedFrames = frameCount_;

        auto now = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(now - startTime_);
        stats.elapsedSeconds = duration.count() / 1000.0;
        stats.avgFps = stats.elapsedSeconds > 0 ? stats.capturedFrames / stats.elapsedSeconds : 0;

        return stats;
    }

    const char* getTypeName() const override {
        return "WindowsGraphicsCaptureRecorder";
    }

    void processFrame() override {
        // Frame processing is handled in FrameArrived callback
    }

private:
    void handleFrameArrived(Direct3D11CaptureFramePool& pool) {
        if (!recording_) return;

        auto frame = pool.TryGetNextFrame();
        if (!frame) {
            droppedFrames_++;
            return;
        }

        // Get the D3D11 surface from the frame
        auto surface = frame.Surface();
        auto interopAccess = surface.as<Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess>();

        ComPtr<ID3D11Texture2D> frameTexture;
        HRESULT hr = interopAccess->GetInterface(IID_PPV_ARGS(&frameTexture));
        if (FAILED(hr)) {
            droppedFrames_++;
            return;
        }

        // Create staging texture if needed
        if (!stagingTexture_) {
            D3D11_TEXTURE2D_DESC desc;
            frameTexture->GetDesc(&desc);

            desc.Usage = D3D11_USAGE_STAGING;
            desc.BindFlags = 0;
            desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            desc.MiscFlags = 0;

            hr = d3dDevice_->CreateTexture2D(&desc, nullptr, &stagingTexture_);
            if (FAILED(hr)) {
                std::cerr << "[WinGraphicsCapture] Failed to create staging texture" << std::endl;
                return;
            }
        }

        // Copy to staging texture
        d3dContext_->CopyResource(stagingTexture_.Get(), frameTexture.Get());

        // Map and write to video
        D3D11_MAPPED_SUBRESOURCE mapped;
        hr = d3dContext_->Map(stagingTexture_.Get(), 0, D3D11_MAP_READ, 0, &mapped);
        if (FAILED(hr)) {
            droppedFrames_++;
            return;
        }

        // Write frame to sink writer
        if (!writeFrame(static_cast<BYTE*>(mapped.pData), mapped.RowPitch)) {
            droppedFrames_++;
        } else {
            frameCount_++;
        }

        d3dContext_->Unmap(stagingTexture_.Get(), 0);
    }

    bool createSinkWriter() {
        // Convert path to wide string
        std::wstring wpath(outputPath_.begin(), outputPath_.end());

        // Create sink writer
        ComPtr<IMFAttributes> attributes;
        HRESULT hr = MFCreateAttributes(&attributes, 1);
        if (FAILED(hr)) return false;

        hr = attributes->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, TRUE);
        if (FAILED(hr)) return false;

        hr = MFCreateSinkWriterFromURL(wpath.c_str(), nullptr, attributes.Get(), &sinkWriter_);
        if (FAILED(hr)) {
            std::cerr << "[WinGraphicsCapture] Failed to create sink writer: " << std::hex << hr << std::endl;
            return false;
        }

        // Set up output type (H.264)
        ComPtr<IMFMediaType> outputType;
        hr = MFCreateMediaType(&outputType);
        if (FAILED(hr)) return false;

        outputType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
        outputType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264);
        outputType->SetUINT32(MF_MT_AVG_BITRATE, 8000000);  // 8 Mbps
        outputType->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
        MFSetAttributeSize(outputType.Get(), MF_MT_FRAME_SIZE, width_, height_);
        MFSetAttributeRatio(outputType.Get(), MF_MT_FRAME_RATE, config_.fps, 1);
        MFSetAttributeRatio(outputType.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1);

        hr = sinkWriter_->AddStream(outputType.Get(), &streamIndex_);
        if (FAILED(hr)) {
            std::cerr << "[WinGraphicsCapture] Failed to add stream: " << std::hex << hr << std::endl;
            return false;
        }

        // Set up input type (BGRA)
        ComPtr<IMFMediaType> inputType;
        hr = MFCreateMediaType(&inputType);
        if (FAILED(hr)) return false;

        inputType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
        inputType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);  // BGRA
        inputType->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
        MFSetAttributeSize(inputType.Get(), MF_MT_FRAME_SIZE, width_, height_);
        MFSetAttributeRatio(inputType.Get(), MF_MT_FRAME_RATE, config_.fps, 1);
        MFSetAttributeRatio(inputType.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1);

        hr = sinkWriter_->SetInputMediaType(streamIndex_, inputType.Get(), nullptr);
        if (FAILED(hr)) {
            std::cerr << "[WinGraphicsCapture] Failed to set input type: " << std::hex << hr << std::endl;
            return false;
        }

        // Start writing
        hr = sinkWriter_->BeginWriting();
        if (FAILED(hr)) {
            std::cerr << "[WinGraphicsCapture] Failed to begin writing: " << std::hex << hr << std::endl;
            return false;
        }

        return true;
    }

    bool writeFrame(BYTE* data, UINT rowPitch) {
        if (!sinkWriter_) return false;

        // Create media sample
        ComPtr<IMFSample> sample;
        HRESULT hr = MFCreateSample(&sample);
        if (FAILED(hr)) return false;

        // Create media buffer
        DWORD bufferSize = height_ * rowPitch;
        ComPtr<IMFMediaBuffer> buffer;
        hr = MFCreateMemoryBuffer(bufferSize, &buffer);
        if (FAILED(hr)) return false;

        // Copy data to buffer
        BYTE* bufferData;
        hr = buffer->Lock(&bufferData, nullptr, nullptr);
        if (FAILED(hr)) return false;

        // Copy row by row (handle potential pitch difference)
        for (UINT row = 0; row < height_; row++) {
            memcpy(bufferData + row * rowPitch, data + row * rowPitch, width_ * 4);
        }

        buffer->Unlock();
        buffer->SetCurrentLength(bufferSize);

        // Add buffer to sample
        sample->AddBuffer(buffer.Get());

        // Set timestamp
        LONGLONG timestamp = (LONGLONG)frameCount_ * 10000000LL / config_.fps;
        sample->SetSampleTime(timestamp);

        // Set duration
        LONGLONG duration = 10000000LL / config_.fps;
        sample->SetSampleDuration(duration);

        // Write sample
        hr = sinkWriter_->WriteSample(streamIndex_, sample.Get());
        return SUCCEEDED(hr);
    }

    // Configuration
    VideoRecorderConfig config_;
    std::string outputPath_;
    UINT width_ = 0;
    UINT height_ = 0;

    // State
    std::atomic<bool> recording_;
    std::chrono::high_resolution_clock::time_point startTime_;
    std::atomic<int> frameCount_;
    std::atomic<int> droppedFrames_;

    // D3D11 objects
    ComPtr<ID3D11Device> d3dDevice_;
    ComPtr<ID3D11DeviceContext> d3dContext_;
    ComPtr<ID3D11Texture2D> stagingTexture_;

    // WinRT capture objects
    IDirect3DDevice winrtDevice_{ nullptr };
    GraphicsCaptureItem captureItem_{ nullptr };
    Direct3D11CaptureFramePool framePool_{ nullptr };
    GraphicsCaptureSession session_{ nullptr };

    // Media Foundation
    bool mfInitialized_ = false;
    ComPtr<IMFSinkWriter> sinkWriter_;
    DWORD streamIndex_ = 0;
};

// Factory function
std::unique_ptr<VideoRecorder> createWindowsGraphicsCaptureRecorder() {
    if (isWindowsGraphicsCaptureAvailable()) {
        return std::make_unique<WindowsGraphicsCaptureRecorder>();
    }
    return nullptr;
}

bool isWindowsGraphicsCaptureAvailableCheck() {
    return isWindowsGraphicsCaptureAvailable();
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
