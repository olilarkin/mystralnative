#pragma once

#include <cstdint>
#include <vector>
#include <queue>
#include <mutex>
#include <atomic>
#include <functional>
#include <memory>

// Forward declare WebGPU types
typedef struct WGPUBufferImpl* WGPUBuffer;
typedef struct WGPUDeviceImpl* WGPUDevice;
typedef struct WGPUQueueImpl* WGPUQueue;
typedef struct WGPUTextureImpl* WGPUTexture;
typedef struct WGPUCommandEncoderImpl* WGPUCommandEncoder;
typedef struct WGPUInstanceImpl* WGPUInstance;

namespace mystral {
namespace video {

/**
 * Captured frame data ready for encoding
 */
struct CapturedFrame {
    std::vector<uint8_t> pixels;  // RGBA pixel data
    uint32_t width;
    uint32_t height;
    int frameNumber;
    double timestamp;  // In seconds
};

/**
 * GPU buffer state for async readback
 */
enum class BufferState {
    Free,           // Available for new capture
    CopyPending,    // GPU copy in progress
    MapPending,     // Waiting for async map
    Mapped,         // Data ready to read
};

/**
 * A single GPU readback buffer with its state
 */
struct ReadbackBuffer {
    WGPUBuffer buffer = nullptr;
    size_t size = 0;
    uint32_t bytesPerRow = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    int frameNumber = -1;
    BufferState state = BufferState::Free;
    std::atomic<bool> mapComplete{false};
    int mapStatus = 0;  // WGPUBufferMapAsyncStatus
};

/**
 * Configuration for async capture
 */
struct AsyncCaptureConfig {
    int initialBufferCount = 8;     // Start with this many GPU buffers
    int maxBufferCount = 16;        // Don't grow beyond this
    int maxQueuedFrames = 24;       // Max frames waiting to be encoded
    int targetFps = 60;             // Target capture framerate
    bool dropFramesOnBackpressure = true;  // Drop old frames when queue is full
};

/**
 * Async Video Capture System
 *
 * Provides non-blocking frame capture for video recording.
 * Uses a pool of GPU readback buffers and a frame queue.
 *
 * Architecture:
 *
 *   [Render Loop]
 *        |
 *        v
 *   [submitCapture()] -- requests GPU copy to buffer
 *        |
 *        v
 *   [Buffer Pool] -- ring of GPU readback buffers
 *        |
 *        | (async map callback)
 *        v
 *   [Frame Queue] -- captured RGBA frames
 *        |
 *        | (encoder thread pulls)
 *        v
 *   [WebP/MP4 Encoder]
 */
class AsyncCapture {
public:
    AsyncCapture();
    ~AsyncCapture();

    /**
     * Initialize the capture system
     * @param device WebGPU device
     * @param queue WebGPU queue
     * @param instance WebGPU instance (for event processing)
     * @param config Configuration options
     * @return true on success
     */
    bool initialize(WGPUDevice device, WGPUQueue queue, WGPUInstance instance,
                    const AsyncCaptureConfig& config = {});

    /**
     * Shutdown and release all resources
     */
    void shutdown();

    /**
     * Submit a frame for capture (non-blocking)
     * Call this after rendering, before present
     * @param sourceTexture The rendered frame texture
     * @param width Frame width
     * @param height Frame height
     * @param frameNumber Current frame number
     * @return true if capture was submitted, false if no buffer available
     */
    bool submitCapture(WGPUTexture sourceTexture, uint32_t width, uint32_t height, int frameNumber);

    /**
     * Process pending async operations (call once per frame)
     * This checks for completed buffer maps and moves data to the frame queue
     */
    void processAsync();

    /**
     * Try to get a captured frame from the queue (non-blocking)
     * @param outFrame Output frame data
     * @return true if a frame was available
     */
    bool tryGetFrame(CapturedFrame& outFrame);

    /**
     * Get number of frames waiting in the queue
     */
    size_t getQueuedFrameCount() const;

    /**
     * Get number of active (non-free) GPU buffers
     */
    int getActiveBufferCount() const;

    /**
     * Get statistics
     */
    struct Stats {
        int capturedFrames = 0;
        int droppedFrames = 0;
        int bufferPoolSize = 0;
        int activeBuffers = 0;
        int queuedFrames = 0;
    };
    Stats getStats() const;

    /**
     * Check if initialized
     */
    bool isInitialized() const { return initialized_; }

private:
    // Find a free buffer or grow the pool
    ReadbackBuffer* acquireBuffer();

    // Create a new readback buffer
    bool createBuffer(ReadbackBuffer& buffer, uint32_t width, uint32_t height);

    // Release a buffer back to the pool
    void releaseBuffer(ReadbackBuffer* buffer);

    // Copy frame data from mapped buffer to frame queue
    void copyToFrameQueue(ReadbackBuffer* buffer);

    // Process a single buffer's async state
    void processBuffer(ReadbackBuffer& buffer);

    WGPUDevice device_ = nullptr;
    WGPUQueue queue_ = nullptr;
    WGPUInstance instance_ = nullptr;

    AsyncCaptureConfig config_;
    bool initialized_ = false;

    // Buffer pool
    std::vector<std::unique_ptr<ReadbackBuffer>> bufferPool_;
    int nextBufferIndex_ = 0;

    // Frame queue (thread-safe)
    std::queue<CapturedFrame> frameQueue_;
    mutable std::mutex queueMutex_;

    // Stats
    std::atomic<int> capturedFrames_{0};
    std::atomic<int> droppedFrames_{0};
};

}  // namespace video
}  // namespace mystral
