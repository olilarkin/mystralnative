#pragma once

/**
 * WorkerThread - Web Worker implementation
 *
 * Each WorkerThread runs its own JavaScript engine in a separate thread,
 * communicating with the main thread via message passing.
 *
 * Usage:
 *   auto worker = std::make_unique<WorkerThread>(id, jsCode);
 *   worker->start();
 *   worker->postMessage(data, transfers);
 *   // ... later ...
 *   while (worker->hasMessages()) {
 *       auto msg = worker->popMessage();
 *       // Handle message from worker
 *   }
 *   worker->terminate();
 */

#include <memory>
#include <string>
#include <vector>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <atomic>
#include <functional>

namespace mystral {
namespace workers {

/**
 * Shared ArrayBuffer data for transfer between threads
 */
struct ArrayBufferData {
    std::vector<uint8_t> data;
    bool transferred = false;  // If true, original is detached
};

/**
 * Message passed between main thread and worker
 */
struct WorkerMessage {
    enum class Type {
        MESSAGE,    // Normal postMessage
        ERROR,      // Error from worker
        TERMINATE   // Termination request
    };

    Type type = Type::MESSAGE;
    std::vector<uint8_t> payload;  // JSON-serialized data
    std::vector<std::shared_ptr<ArrayBufferData>> transfers;
};

/**
 * Callback for receiving messages from a worker
 */
using WorkerMessageCallback = std::function<void(int workerId, WorkerMessage msg)>;

/**
 * WorkerThread - Runs JS code in a separate thread
 */
class WorkerThread {
public:
    /**
     * Create a worker thread
     * @param id Unique worker ID
     * @param code JavaScript code to execute
     */
    WorkerThread(int id, const std::string& code);
    ~WorkerThread();

    /**
     * Start the worker thread
     */
    void start();

    /**
     * Post a message to the worker
     * @param data JSON-serialized message data
     * @param transfers ArrayBuffers to transfer (not copy)
     */
    void postMessage(std::vector<uint8_t> data,
                     std::vector<std::shared_ptr<ArrayBufferData>> transfers = {});

    /**
     * Terminate the worker
     */
    void terminate();

    /**
     * Check if the worker has messages to process
     */
    bool hasMessages() const;

    /**
     * Pop a message from the worker's output queue
     */
    WorkerMessage popMessage();

    /**
     * Check if the worker is still running
     */
    bool isRunning() const { return running_.load(); }

    /**
     * Get the worker ID
     */
    int getId() const { return id_; }

private:
    int id_;
    std::string code_;
    std::unique_ptr<std::thread> thread_;

    // Thread-safe message queues
    mutable std::mutex inMutex_;
    mutable std::mutex outMutex_;
    std::queue<WorkerMessage> inQueue_;   // Main -> Worker
    std::queue<WorkerMessage> outQueue_;  // Worker -> Main
    std::condition_variable inCondition_;

    std::atomic<bool> running_{false};
    std::atomic<bool> terminated_{false};

    void threadMain();
    void processMessages(void* engine);  // void* is js::Engine*
    void setupWorkerGlobals(void* engine);
};

}  // namespace workers
}  // namespace mystral
