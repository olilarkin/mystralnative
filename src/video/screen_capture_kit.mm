/**
 * ScreenCaptureKit Video Recorder (macOS 12.3+)
 *
 * Uses Apple's ScreenCaptureKit framework for high-quality, low-overhead screen capture.
 * Captures the SDL window directly and encodes to H.264/MP4 using AVAssetWriter.
 *
 * Requirements:
 * - macOS 12.3 (Monterey) or later
 * - Screen Recording permission (user will be prompted)
 *
 * Architecture:
 * - SCContentFilter: Selects the specific window to capture
 * - SCStream: Provides CMSampleBuffers with pixel data
 * - AVAssetWriter: Encodes to H.264 MP4
 */

#include "mystral/video/video_recorder.h"

#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

// Check if we're on macOS (not iOS)
#if defined(__APPLE__) && TARGET_OS_MAC && !TARGET_OS_IPHONE

#import <Foundation/Foundation.h>
#import <ScreenCaptureKit/ScreenCaptureKit.h>
#import <AVFoundation/AVFoundation.h>
#import <CoreMedia/CoreMedia.h>
#import <CoreVideo/CoreVideo.h>
#import <SDL3/SDL.h>
#import <Cocoa/Cocoa.h>

#include <atomic>
#include <chrono>
#include <iostream>
#include <mutex>

// Check for macOS 12.3+ at runtime
static bool isScreenCaptureKitAvailable() {
    if (@available(macOS 12.3, *)) {
        return true;
    }
    return false;
}

// Forward declare the C++ class so the delegate can reference it
namespace mystral {
namespace video {
class ScreenCaptureKitRecorder;
}
}

/**
 * Objective-C delegate for SCStream output (must be at global scope)
 */
@interface SCKStreamOutputDelegate : NSObject <SCStreamOutput>
@property (nonatomic, assign) void* recorder;
@end

namespace mystral {
namespace video {

/**
 * ScreenCaptureKit Video Recorder Implementation
 */
class ScreenCaptureKitRecorder : public VideoRecorder {
public:
    ScreenCaptureKitRecorder()
        : recording_(false), frameCount_(0), droppedFrames_(0) {}

    ~ScreenCaptureKitRecorder() override {
        if (isRecording()) {
            stopRecording();
        }
    }

    bool startRecording(void* nativeWindowHandle,
                        const std::string& outputPath,
                        const VideoRecorderConfig& config) override {
        if (@available(macOS 12.3, *)) {
            return startRecordingImpl(nativeWindowHandle, outputPath, config);
        }
        std::cerr << "[ScreenCaptureKit] Requires macOS 12.3 or later" << std::endl;
        return false;
    }

    bool stopRecording() override {
        if (@available(macOS 12.3, *)) {
            return stopRecordingImpl();
        }
        return false;
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
        return "ScreenCaptureKitRecorder";
    }

    void processFrame() override {
        // ScreenCaptureKit handles frame capture on its own thread
        // This is a no-op for this implementation
    }

    // Called by delegate when a frame is received
    void handleSampleBuffer(CMSampleBufferRef sampleBuffer) API_AVAILABLE(macos(12.3)) {
        if (!recording_) return;

        std::lock_guard<std::mutex> lock(writerMutex_);

        if (!assetWriter_ || !videoInput_) return;

        // Check if the writer is ready
        if (assetWriter_.status != AVAssetWriterStatusWriting) {
            if (assetWriter_.status == AVAssetWriterStatusFailed) {
                std::cerr << "[ScreenCaptureKit] Asset writer failed: "
                          << [assetWriter_.error.localizedDescription UTF8String] << std::endl;
                recording_ = false;
            }
            return;
        }

        // Get the pixel buffer
        CVPixelBufferRef pixelBuffer = CMSampleBufferGetImageBuffer(sampleBuffer);
        if (!pixelBuffer) {
            droppedFrames_++;
            return;
        }

        // Get presentation timestamp
        CMTime pts = CMSampleBufferGetPresentationTimeStamp(sampleBuffer);

        // Calculate relative timestamp from start
        if (firstTimestamp_.value == 0) {
            firstTimestamp_ = pts;
        }
        CMTime relativePts = CMTimeSubtract(pts, firstTimestamp_);

        // Append to video input
        if ([videoInput_ isReadyForMoreMediaData]) {
            if (![adaptor_ appendPixelBuffer:pixelBuffer withPresentationTime:relativePts]) {
                std::cerr << "[ScreenCaptureKit] Failed to append pixel buffer" << std::endl;
                droppedFrames_++;
            } else {
                frameCount_++;
            }
        } else {
            droppedFrames_++;
        }
    }

private:
    bool startRecordingImpl(void* nativeWindowHandle,
                           const std::string& outputPath,
                           const VideoRecorderConfig& config) API_AVAILABLE(macos(12.3)) {
        if (recording_) {
            std::cerr << "[ScreenCaptureKit] Already recording" << std::endl;
            return false;
        }

        // Get SDL window
        SDL_Window* sdlWindow = static_cast<SDL_Window*>(nativeWindowHandle);
        if (!sdlWindow) {
            std::cerr << "[ScreenCaptureKit] Invalid window handle" << std::endl;
            return false;
        }

        // Get NSWindow from SDL
        SDL_PropertiesID props = SDL_GetWindowProperties(sdlWindow);
        NSWindow* nsWindow = (__bridge NSWindow*)SDL_GetPointerProperty(props,
            SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, NULL);
        if (!nsWindow) {
            std::cerr << "[ScreenCaptureKit] Failed to get NSWindow from SDL" << std::endl;
            return false;
        }

        // Get window ID
        CGWindowID targetWindowID = (CGWindowID)[nsWindow windowNumber];

        // Store config
        config_ = config;
        outputPath_ = outputPath;
        frameCount_ = 0;
        droppedFrames_ = 0;
        firstTimestamp_ = CMTimeMake(0, 1);

        // Get window size
        NSRect frame = [nsWindow frame];
        int width = config.width > 0 ? config.width : (int)frame.size.width;
        int height = config.height > 0 ? config.height : (int)frame.size.height;

        // Use dispatch semaphore for async completion
        dispatch_semaphore_t semaphore = dispatch_semaphore_create(0);
        __block bool setupSuccess = false;
        __block SCWindow* targetWindow = nil;

        // Retry finding the window a few times (it may not be registered with the window server yet)
        for (int attempt = 0; attempt < 10 && !setupSuccess; attempt++) {
            if (attempt > 0) {
                // Wait a bit before retrying
                usleep(100000);  // 100ms
                std::cout << "[ScreenCaptureKit] Retrying window search (attempt " << (attempt + 1) << ")..." << std::endl;
            }

            // Get shareable content
            [SCShareableContent getShareableContentExcludingDesktopWindows:YES
                                                       onScreenWindowsOnly:NO  // Include off-screen windows
                                                         completionHandler:^(SCShareableContent* content, NSError* error) {
                if (error) {
                    std::cerr << "[ScreenCaptureKit] Failed to get shareable content: "
                              << [error.localizedDescription UTF8String] << std::endl;
                    if ([error.localizedDescription containsString:@"declined"]) {
                        std::cerr << "[ScreenCaptureKit] Screen recording permission required." << std::endl;
                        std::cerr << "[ScreenCaptureKit] Go to System Preferences > Privacy & Security > Screen Recording" << std::endl;
                        std::cerr << "[ScreenCaptureKit] and enable permission for this application." << std::endl;
                    }
                    dispatch_semaphore_signal(semaphore);
                    return;
                }

                // Find our window
                for (SCWindow* scWindow in content.windows) {
                    if (scWindow.windowID == targetWindowID) {
                        targetWindow = scWindow;
                        setupSuccess = true;
                        break;
                    }
                }

                if (!setupSuccess && attempt == 9) {
                    // Last attempt failed, log available windows
                    std::cerr << "[ScreenCaptureKit] Window not found in shareable content" << std::endl;
                    std::cerr << "[ScreenCaptureKit] Target window ID: " << targetWindowID << std::endl;
                    std::cerr << "[ScreenCaptureKit] Available windows:" << std::endl;
                    for (SCWindow* scWindow in content.windows) {
                        std::cerr << "  - ID: " << scWindow.windowID
                                  << " Title: " << [scWindow.title UTF8String] << std::endl;
                    }
                }

                dispatch_semaphore_signal(semaphore);
            }];

            // Wait for async completion with timeout
            if (dispatch_semaphore_wait(semaphore, dispatch_time(DISPATCH_TIME_NOW, 2 * NSEC_PER_SEC)) != 0) {
                std::cerr << "[ScreenCaptureKit] Timeout getting shareable content" << std::endl;
            }
        }

        if (!setupSuccess || !targetWindow) {
            return false;
        }

        // Create content filter for the window
        SCContentFilter* filter = [[SCContentFilter alloc] initWithDesktopIndependentWindow:targetWindow];

        // Configure stream
        SCStreamConfiguration* streamConfig = [[SCStreamConfiguration alloc] init];
        streamConfig.width = width;
        streamConfig.height = height;
        streamConfig.minimumFrameInterval = CMTimeMake(1, config.fps);
        streamConfig.pixelFormat = kCVPixelFormatType_32BGRA;
        streamConfig.capturesAudio = NO;

        // Create stream
        stream_ = [[SCStream alloc] initWithFilter:filter configuration:streamConfig delegate:nil];

        // Create output delegate
        outputDelegate_ = [[SCKStreamOutputDelegate alloc] init];
        outputDelegate_.recorder = this;

        // Add output
        NSError* error = nil;
        if (![stream_ addStreamOutput:outputDelegate_ type:SCStreamOutputTypeScreen sampleHandlerQueue:dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_HIGH, 0) error:&error]) {
            std::cerr << "[ScreenCaptureKit] Failed to add stream output: "
                      << [error.localizedDescription UTF8String] << std::endl;
            return false;
        }

        // Set up AVAssetWriter for MP4 output
        NSURL* outputURL = [NSURL fileURLWithPath:[NSString stringWithUTF8String:outputPath.c_str()]];

        // Delete existing file
        [[NSFileManager defaultManager] removeItemAtURL:outputURL error:nil];

        assetWriter_ = [AVAssetWriter assetWriterWithURL:outputURL fileType:AVFileTypeMPEG4 error:&error];
        if (error) {
            std::cerr << "[ScreenCaptureKit] Failed to create asset writer: "
                      << [error.localizedDescription UTF8String] << std::endl;
            return false;
        }

        // Video output settings (H.264)
        NSDictionary* videoSettings = @{
            AVVideoCodecKey: AVVideoCodecTypeH264,
            AVVideoWidthKey: @(width),
            AVVideoHeightKey: @(height),
            AVVideoCompressionPropertiesKey: @{
                AVVideoAverageBitRateKey: @(8000000),  // 8 Mbps
                AVVideoProfileLevelKey: AVVideoProfileLevelH264HighAutoLevel,
                AVVideoMaxKeyFrameIntervalKey: @(config.fps * 2),
            }
        };

        videoInput_ = [AVAssetWriterInput assetWriterInputWithMediaType:AVMediaTypeVideo
                                                         outputSettings:videoSettings];
        videoInput_.expectsMediaDataInRealTime = YES;

        // Create pixel buffer adaptor
        NSDictionary* sourcePixelBufferAttributes = @{
            (NSString*)kCVPixelBufferPixelFormatTypeKey: @(kCVPixelFormatType_32BGRA),
            (NSString*)kCVPixelBufferWidthKey: @(width),
            (NSString*)kCVPixelBufferHeightKey: @(height),
        };

        adaptor_ = [AVAssetWriterInputPixelBufferAdaptor
            assetWriterInputPixelBufferAdaptorWithAssetWriterInput:videoInput_
                                      sourcePixelBufferAttributes:sourcePixelBufferAttributes];

        if (![assetWriter_ canAddInput:videoInput_]) {
            std::cerr << "[ScreenCaptureKit] Cannot add video input to asset writer" << std::endl;
            return false;
        }

        [assetWriter_ addInput:videoInput_];

        // Start writing
        if (![assetWriter_ startWriting]) {
            std::cerr << "[ScreenCaptureKit] Failed to start writing: "
                      << [assetWriter_.error.localizedDescription UTF8String] << std::endl;
            return false;
        }

        [assetWriter_ startSessionAtSourceTime:kCMTimeZero];

        // Start capture
        dispatch_semaphore_t startSemaphore = dispatch_semaphore_create(0);
        __block bool startSuccess = false;

        [stream_ startCaptureWithCompletionHandler:^(NSError* startError) {
            if (startError) {
                std::cerr << "[ScreenCaptureKit] Failed to start capture: "
                          << [startError.localizedDescription UTF8String] << std::endl;
            } else {
                startSuccess = true;
            }
            dispatch_semaphore_signal(startSemaphore);
        }];

        dispatch_semaphore_wait(startSemaphore, dispatch_time(DISPATCH_TIME_NOW, 5 * NSEC_PER_SEC));

        if (!startSuccess) {
            return false;
        }

        startTime_ = std::chrono::high_resolution_clock::now();
        recording_ = true;

        std::cout << "[ScreenCaptureKit] Started recording " << width << "x" << height
                  << " @ " << config.fps << "fps to " << outputPath << std::endl;

        return true;
    }

    bool stopRecordingImpl() API_AVAILABLE(macos(12.3)) {
        if (!recording_) {
            return false;
        }

        recording_ = false;

        // Stop capture
        dispatch_semaphore_t stopSemaphore = dispatch_semaphore_create(0);

        [stream_ stopCaptureWithCompletionHandler:^(NSError* error) {
            if (error) {
                std::cerr << "[ScreenCaptureKit] Error stopping capture: "
                          << [error.localizedDescription UTF8String] << std::endl;
            }
            dispatch_semaphore_signal(stopSemaphore);
        }];

        dispatch_semaphore_wait(stopSemaphore, dispatch_time(DISPATCH_TIME_NOW, 5 * NSEC_PER_SEC));

        // Finalize video
        {
            std::lock_guard<std::mutex> lock(writerMutex_);

            [videoInput_ markAsFinished];

            dispatch_semaphore_t finishSemaphore = dispatch_semaphore_create(0);
            [assetWriter_ finishWritingWithCompletionHandler:^{
                dispatch_semaphore_signal(finishSemaphore);
            }];

            dispatch_semaphore_wait(finishSemaphore, dispatch_time(DISPATCH_TIME_NOW, 10 * NSEC_PER_SEC));

            if (assetWriter_.status == AVAssetWriterStatusFailed) {
                std::cerr << "[ScreenCaptureKit] Failed to finalize video: "
                          << [assetWriter_.error.localizedDescription UTF8String] << std::endl;
                return false;
            }
        }

        // Cleanup
        stream_ = nil;
        outputDelegate_ = nil;
        assetWriter_ = nil;
        videoInput_ = nil;
        adaptor_ = nil;

        std::cout << "[ScreenCaptureKit] Stopped recording. Captured " << frameCount_
                  << " frames, dropped " << droppedFrames_ << std::endl;

        return true;
    }

    // Configuration
    VideoRecorderConfig config_;
    std::string outputPath_;

    // State
    std::atomic<bool> recording_;
    std::chrono::high_resolution_clock::time_point startTime_;
    std::atomic<int> frameCount_;
    std::atomic<int> droppedFrames_;
    CMTime firstTimestamp_;
    std::mutex writerMutex_;

    // ScreenCaptureKit objects (only valid on macOS 12.3+)
    SCStream* stream_ API_AVAILABLE(macos(12.3)) = nil;
    SCKStreamOutputDelegate* outputDelegate_ API_AVAILABLE(macos(12.3)) = nil;

    // AVFoundation objects
    AVAssetWriter* assetWriter_ = nil;
    AVAssetWriterInput* videoInput_ = nil;
    AVAssetWriterInputPixelBufferAdaptor* adaptor_ = nil;
};

// Factory function
std::unique_ptr<VideoRecorder> createScreenCaptureKitRecorder() {
    if (isScreenCaptureKitAvailable()) {
        return std::make_unique<ScreenCaptureKitRecorder>();
    }
    return nullptr;
}

bool isScreenCaptureKitAvailableCheck() {
    return isScreenCaptureKitAvailable();
}

}  // namespace video
}  // namespace mystral

// Delegate implementation (must be at global scope)
@implementation SCKStreamOutputDelegate

- (void)stream:(SCStream*)stream didOutputSampleBuffer:(CMSampleBufferRef)sampleBuffer ofType:(SCStreamOutputType)type API_AVAILABLE(macos(12.3)) {
    if (type == SCStreamOutputTypeScreen && self.recorder) {
        static_cast<mystral::video::ScreenCaptureKitRecorder*>(self.recorder)->handleSampleBuffer(sampleBuffer);
    }
}

@end

#else  // Not macOS (iOS, Linux, Windows, etc.)

namespace mystral {
namespace video {

std::unique_ptr<VideoRecorder> createScreenCaptureKitRecorder() {
    return nullptr;
}

bool isScreenCaptureKitAvailableCheck() {
    return false;
}

}  // namespace video
}  // namespace mystral

#endif  // __APPLE__ && TARGET_OS_MAC && !TARGET_OS_IPHONE
