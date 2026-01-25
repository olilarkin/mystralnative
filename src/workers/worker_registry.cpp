/**
 * WorkerRegistry Implementation
 *
 * Singleton that manages all web workers and routes messages
 * between the main thread and worker threads.
 */

#include "mystral/workers/worker_registry.h"
#include <iostream>

namespace mystral {
namespace workers {

WorkerRegistry& WorkerRegistry::instance() {
    static WorkerRegistry instance;
    return instance;
}

WorkerRegistry::WorkerRegistry() {
    initialized_ = true;
    std::cout << "[WorkerRegistry] Initialized" << std::endl;
}

WorkerRegistry::~WorkerRegistry() {
    shutdown();
}

bool WorkerRegistry::isAvailable() const {
    return initialized_;
}

int WorkerRegistry::createWorker(const std::string& code) {
    std::lock_guard<std::mutex> lock(mutex_);

    int id = nextId_++;

    auto worker = std::make_unique<WorkerThread>(id, code);
    worker->start();

    workers_[id] = std::move(worker);

    std::cout << "[WorkerRegistry] Created worker " << id << std::endl;

    return id;
}

void WorkerRegistry::postToWorker(int id, WorkerMessage msg) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = workers_.find(id);
    if (it == workers_.end()) {
        std::cerr << "[WorkerRegistry] Worker " << id << " not found" << std::endl;
        return;
    }

    it->second->postMessage(std::move(msg.payload), std::move(msg.transfers));
}

void WorkerRegistry::terminateWorker(int id) {
    std::unique_ptr<WorkerThread> worker;

    {
        std::lock_guard<std::mutex> lock(mutex_);

        auto it = workers_.find(id);
        if (it == workers_.end()) {
            return;
        }

        worker = std::move(it->second);
        workers_.erase(it);
        callbacks_.erase(id);
    }

    // Terminate outside lock to avoid deadlock
    if (worker) {
        worker->terminate();
        std::cout << "[WorkerRegistry] Terminated worker " << id << std::endl;
    }
}

void WorkerRegistry::registerCallback(int id, JSWorkerCallback callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    callbacks_[id] = std::move(callback);
}

void WorkerRegistry::unregisterCallback(int id) {
    std::lock_guard<std::mutex> lock(mutex_);
    callbacks_.erase(id);
}

bool WorkerRegistry::processWorkerMessages(js::Engine* mainEngine) {
    if (!mainEngine) {
        return false;
    }

    bool hadMessages = false;

    // Collect messages and dead workers (hold lock briefly)
    std::vector<std::tuple<int, JSWorkerCallback, WorkerMessage>> messages;
    std::vector<int> deadWorkers;

    {
        std::lock_guard<std::mutex> lock(mutex_);

        for (auto& [id, worker] : workers_) {
            if (!worker->isRunning()) {
                deadWorkers.push_back(id);
                continue;
            }

            auto callbackIt = callbacks_.find(id);
            if (callbackIt == callbacks_.end()) continue;

            // Collect all messages from this worker
            while (worker->hasMessages()) {
                messages.emplace_back(id, callbackIt->second, worker->popMessage());
            }
        }
    }

    // Invoke callbacks outside the lock
    for (auto& [id, callback, msg] : messages) {
        hadMessages = true;
        try {
            callback(id, msg);
        } catch (const std::exception& e) {
            std::cerr << "[WorkerRegistry] Error in callback for worker " << id
                      << ": " << e.what() << std::endl;
        }
    }

    // Cleanup dead workers
    for (int id : deadWorkers) {
        terminateWorker(id);
    }

    return hadMessages;
}

void WorkerRegistry::shutdown() {
    std::vector<int> ids;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& [id, _] : workers_) {
            ids.push_back(id);
        }
    }

    for (int id : ids) {
        terminateWorker(id);
    }

    initialized_ = false;
    std::cout << "[WorkerRegistry] Shutdown complete" << std::endl;
}

}  // namespace workers
}  // namespace mystral
