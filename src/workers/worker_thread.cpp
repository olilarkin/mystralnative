/**
 * WorkerThread Implementation
 *
 * Runs JavaScript code in a separate thread with its own JS engine.
 * Communicates with the main thread via thread-safe message queues.
 */

#include "mystral/workers/worker_thread.h"
#include "mystral/js/engine.h"
#include <iostream>
#include <chrono>

namespace mystral {
namespace workers {

// Global pointer used by worker's native functions to access the engine
// Thread-local to support multiple workers
thread_local js::Engine* g_workerEngine = nullptr;
thread_local WorkerThread* g_workerThread = nullptr;

WorkerThread::WorkerThread(int id, const std::string& code)
    : id_(id)
    , code_(code)
{
}

WorkerThread::~WorkerThread() {
    terminate();
    if (thread_ && thread_->joinable()) {
        thread_->join();
    }
}

void WorkerThread::start() {
    if (running_.load()) {
        return;
    }

    running_ = true;
    thread_ = std::make_unique<std::thread>(&WorkerThread::threadMain, this);
}

void WorkerThread::postMessage(std::vector<uint8_t> data,
                               std::vector<std::shared_ptr<ArrayBufferData>> transfers) {
    if (terminated_.load()) {
        return;
    }

    WorkerMessage msg;
    msg.type = WorkerMessage::Type::MESSAGE;
    msg.payload = std::move(data);
    msg.transfers = std::move(transfers);

    {
        std::lock_guard<std::mutex> lock(inMutex_);
        inQueue_.push(std::move(msg));
    }
    inCondition_.notify_one();
}

void WorkerThread::terminate() {
    if (terminated_.exchange(true)) {
        return;  // Already terminated
    }

    // Send termination message
    {
        std::lock_guard<std::mutex> lock(inMutex_);
        WorkerMessage msg;
        msg.type = WorkerMessage::Type::TERMINATE;
        inQueue_.push(std::move(msg));
    }
    inCondition_.notify_one();

    // Wait for thread to finish
    if (thread_ && thread_->joinable()) {
        thread_->join();
    }

    running_ = false;
}

bool WorkerThread::hasMessages() const {
    std::lock_guard<std::mutex> lock(outMutex_);
    return !outQueue_.empty();
}

WorkerMessage WorkerThread::popMessage() {
    std::lock_guard<std::mutex> lock(outMutex_);
    if (outQueue_.empty()) {
        return WorkerMessage{};
    }
    WorkerMessage msg = std::move(outQueue_.front());
    outQueue_.pop();
    return msg;
}

void WorkerThread::setupWorkerGlobals(void* enginePtr) {
    auto* engine = static_cast<js::Engine*>(enginePtr);

    // __workerPostMessage(jsonString, transfers) - Send message to main thread
    engine->setGlobalProperty("__workerPostMessage",
        engine->newFunction("__workerPostMessage",
            [](void* ctx, const std::vector<js::JSValueHandle>& args) {
                if (!g_workerEngine || !g_workerThread) {
                    return g_workerEngine->newUndefined();
                }

                if (args.empty()) {
                    return g_workerEngine->newUndefined();
                }

                // Get JSON string payload
                std::string json = g_workerEngine->toString(args[0]);

                WorkerMessage msg;
                msg.type = WorkerMessage::Type::MESSAGE;
                msg.payload = std::vector<uint8_t>(json.begin(), json.end());

                // Handle transfers (ArrayBuffers)
                if (args.size() > 1 && g_workerEngine->isArray(args[1])) {
                    // TODO: Extract ArrayBuffers and mark as transferred
                }

                // Queue message for main thread
                {
                    std::lock_guard<std::mutex> lock(g_workerThread->outMutex_);
                    g_workerThread->outQueue_.push(std::move(msg));
                }

                return g_workerEngine->newUndefined();
            }
        )
    );

    // __workerClose() - Self-terminate the worker
    engine->setGlobalProperty("__workerClose",
        engine->newFunction("__workerClose",
            [](void* ctx, const std::vector<js::JSValueHandle>& args) {
                if (g_workerThread) {
                    g_workerThread->terminated_ = true;
                }
                return g_workerEngine->newUndefined();
            }
        )
    );

    // __workerHasMessage() - Check if there's a message in the queue
    engine->setGlobalProperty("__workerHasMessage",
        engine->newFunction("__workerHasMessage",
            [](void* ctx, const std::vector<js::JSValueHandle>& args) {
                if (!g_workerThread) {
                    return g_workerEngine->newBoolean(false);
                }
                std::lock_guard<std::mutex> lock(g_workerThread->inMutex_);
                return g_workerEngine->newBoolean(!g_workerThread->inQueue_.empty());
            }
        )
    );

    // __workerGetMessage() - Get the next message from the queue (blocking or non-blocking)
    engine->setGlobalProperty("__workerGetMessage",
        engine->newFunction("__workerGetMessage",
            [](void* ctx, const std::vector<js::JSValueHandle>& args) {
                if (!g_workerThread || !g_workerEngine) {
                    return g_workerEngine->newNull();
                }

                bool blocking = true;
                if (!args.empty()) {
                    blocking = g_workerEngine->toBoolean(args[0]);
                }

                WorkerMessage msg;
                {
                    std::unique_lock<std::mutex> lock(g_workerThread->inMutex_);

                    if (blocking) {
                        // Wait for a message with timeout (100ms)
                        g_workerThread->inCondition_.wait_for(lock,
                            std::chrono::milliseconds(100),
                            [&]() {
                                return !g_workerThread->inQueue_.empty() ||
                                       g_workerThread->terminated_.load();
                            }
                        );
                    }

                    if (g_workerThread->inQueue_.empty()) {
                        return g_workerEngine->newNull();
                    }

                    msg = std::move(g_workerThread->inQueue_.front());
                    g_workerThread->inQueue_.pop();
                }

                // Create result object
                auto result = g_workerEngine->newObject();
                g_workerEngine->setProperty(result, "type",
                    g_workerEngine->newNumber(static_cast<int>(msg.type)));

                if (!msg.payload.empty()) {
                    std::string json(msg.payload.begin(), msg.payload.end());
                    g_workerEngine->setProperty(result, "data",
                        g_workerEngine->newString(json.c_str()));
                }

                // TODO: Handle transferred ArrayBuffers

                return result;
            }
        )
    );

    // Worker global scope setup (JavaScript)
    const char* workerGlobalCode = R"(
// Worker global scope - make self a global reference to globalThis
globalThis.self = globalThis;

// Private state (using closure via IIFE to hide internals)
(function() {
    let _onmessage = null;
    let _onerror = null;

    // onmessage property on globalThis (accessible as self.onmessage)
    Object.defineProperty(globalThis, 'onmessage', {
        get: () => _onmessage,
        set: (fn) => {
            _onmessage = fn;
        },
        configurable: true
    });

    // onerror property
    Object.defineProperty(globalThis, 'onerror', {
        get: () => _onerror,
        set: (fn) => { _onerror = fn; },
        configurable: true
    });

    // postMessage function
    globalThis.postMessage = function(data, transfer) {
        transfer = transfer || [];
        const json = JSON.stringify(data);
        __workerPostMessage(json, transfer);
    };

    // close function
    globalThis.close = function() {
        __workerClose();
    };

    // Internal: Process incoming messages
    globalThis.__processMessages = function() {
        while (true) {
            const msg = __workerGetMessage(false);  // Non-blocking
            if (!msg) break;

            if (msg.type === 2) {  // TERMINATE
                globalThis.close();
                return false;
            }

            if (msg.type === 0 && _onmessage) {  // MESSAGE
                try {
                    const data = msg.data ? JSON.parse(msg.data) : undefined;
                    _onmessage({ data: data, target: globalThis });
                } catch (e) {
                    console.error('[Worker] Error processing message:', e);
                    if (_onerror) {
                        _onerror({ error: e, message: e.message });
                    }
                }
            }
        }
        return true;
    };
})();
)";

    engine->eval(workerGlobalCode, "worker-global.js");
}

void WorkerThread::threadMain() {
    std::cout << "[Worker " << id_ << "] Thread started" << std::endl;

    // Create a new JS engine for this worker
    auto engine = js::createEngine();
    if (!engine) {
        std::cerr << "[Worker " << id_ << "] Failed to create JS engine" << std::endl;
        running_ = false;

        // Send error to main thread
        WorkerMessage errMsg;
        errMsg.type = WorkerMessage::Type::ERROR;
        std::string error = "Failed to create JS engine";
        errMsg.payload = std::vector<uint8_t>(error.begin(), error.end());
        {
            std::lock_guard<std::mutex> lock(outMutex_);
            outQueue_.push(std::move(errMsg));
        }
        return;
    }

    // Set thread-local globals
    g_workerEngine = engine.get();
    g_workerThread = this;

    // Add worker log function FIRST (before anything uses console)
    engine->setGlobalProperty("__workerLog",
        engine->newFunction("__workerLog",
            [](void* ctx, const std::vector<js::JSValueHandle>& args) {
                if (args.size() < 2) return g_workerEngine->newUndefined();

                std::string level = g_workerEngine->toString(args[0]);
                std::string msg = g_workerEngine->toString(args[1]);

                int workerId = g_workerThread ? g_workerThread->getId() : -1;
                std::cout << "[Worker " << workerId << "] [" << level << "] " << msg << std::endl;

                return g_workerEngine->newUndefined();
            }
        )
    );

    // Force console override for workers (always replace, even if exists)
    const char* consoleCode = R"(
globalThis.console = {
    log: (...args) => __workerLog('log', args.join(' ')),
    warn: (...args) => __workerLog('warn', args.join(' ')),
    error: (...args) => __workerLog('error', args.join(' ')),
    info: (...args) => __workerLog('info', args.join(' ')),
};
)";

    engine->eval(consoleCode, "worker-console.js");

    // Setup worker globals (after console is available)
    setupWorkerGlobals(engine.get());

    // Execute the worker code
    std::cout << "[Worker " << id_ << "] Executing user code..." << std::endl;
    if (!engine->eval(code_.c_str(), "worker.js")) {
        std::string error = engine->getException();
        std::cerr << "[Worker " << id_ << "] Error executing code: " << error << std::endl;

        WorkerMessage errMsg;
        errMsg.type = WorkerMessage::Type::ERROR;
        errMsg.payload = std::vector<uint8_t>(error.begin(), error.end());
        {
            std::lock_guard<std::mutex> lock(outMutex_);
            outQueue_.push(std::move(errMsg));
        }
    } else {
        std::cout << "[Worker " << id_ << "] User code executed successfully" << std::endl;
    }

    // Check for any pending exception
    if (engine->hasException()) {
        std::string error = engine->getException();
        std::cerr << "[Worker " << id_ << "] Exception after code execution: " << error << std::endl;
    }

    std::cout << "[Worker " << id_ << "] Entering main loop..." << std::endl;

    // Main worker loop
    while (!terminated_.load()) {
        // Process messages via JS
        auto processResult = engine->evalWithResult("__processMessages()", "worker-loop.js");
        if (engine->hasException()) {
            std::string error = engine->getException();
            std::cerr << "[Worker " << id_ << "] Exception in message loop: " << error << std::endl;
        }
        if (!engine->toBoolean(processResult)) {
            std::cout << "[Worker " << id_ << "] __processMessages returned false, exiting" << std::endl;
            break;  // Worker requested close
        }

        // Small sleep to prevent busy-waiting
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    // Cleanup
    g_workerEngine = nullptr;
    g_workerThread = nullptr;
    running_ = false;

    std::cout << "[Worker " << id_ << "] Thread finished" << std::endl;
}

}  // namespace workers
}  // namespace mystral
