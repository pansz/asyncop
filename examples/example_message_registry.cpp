/**
 * @file example_message_registry.cpp
 * @brief Example of using MessageRegistry for request/response patterns
 * 
 * This example demonstrates how to use the MessageRegistry to convert
 * traditional request/response callback patterns to AsyncOp.
 */

#include "async_op.hpp"
#include "msg_registry.hpp"
#include <iostream>
#include <functional>
#include <chrono>
#include <thread>
#include <spdlog/spdlog.h>
#include <queue>
#include <mutex>

// Example message types
struct Request {
    int64_t id;
    std::string operation;
    std::string data;
};

struct Response {
    int64_t id;
    bool success;
    std::string data;
    std::string errorMessage;
};

// Simulated network layer
class NetworkSimulator {
public:
    // Singleton instance
    static NetworkSimulator& getInstance() {
        static NetworkSimulator instance;
        return instance;
    }
    
    // Send a request and register it with the callback
    void sendRequest(int64_t msgId, const std::string& operation, const std::string& data) {
        spdlog::info("Network: Sending request ID {} for operation '{}'", msgId, operation);
        
        // Simulate network delay and processing
        std::thread([msgId, operation, data, this]() {
            // Variable delay based on operation type
            int delay = 50 + (msgId % 100);
            std::this_thread::sleep_for(std::chrono::milliseconds(delay));
            
            // Simulate success/failure based on operation type
            bool success = (msgId % 7) != 0; // ~14% failure rate
            
            // Generate response
            Response response;
            response.id = msgId;
            response.success = success;
            response.data = "Result for " + operation + " with data: " + data;
            response.errorMessage = success ? "" : "Simulated network error";
            
            // Queue response to be processed
            {
                std::lock_guard<std::mutex> lock(queueMutex_);
                responseQueue_.push(response);
            }
            
            // Wake up the processing loop
            processResponses();
        }).detach();
    }

    // Process responses and dispatch them
    void processResponses() {
        std::queue<Response> localQueue;
        {
            std::lock_guard<std::mutex> lock(queueMutex_);
            localQueue.swap(responseQueue_);
        }
        
        while (!localQueue.empty()) {
            Response resp = localQueue.front();
            localQueue.pop();
            
            // Dispatch to the appropriate handler
            dispatchResponse(resp);
        }
    }
    
    // Register a response handler
    void registerResponseHandler(int64_t msgId, std::function<void(Response)> handler) {
        std::lock_guard<std::mutex> lock(handlersMutex_);
        responseHandlers_[msgId] = handler;
    }
    
    // Remove a response handler
    void unregisterResponseHandler(int64_t msgId) {
        std::lock_guard<std::mutex> lock(handlersMutex_);
        responseHandlers_.erase(msgId);
    }

private:
    std::queue<Response> responseQueue_;
    std::mutex queueMutex_;
    std::unordered_map<int64_t, std::function<void(Response)>> responseHandlers_;
    std::mutex handlersMutex_;
    
    void dispatchResponse(const Response& response) {
        std::function<void(Response)> handler;
        {
            std::lock_guard<std::mutex> lock(handlersMutex_);
            auto it = responseHandlers_.find(response.id);
            if (it != responseHandlers_.end()) {
                handler = it->second;
                responseHandlers_.erase(it); // Remove handler after use
            }
        }
        
        if (handler) {
            handler(response);
        } else {
            spdlog::warn("No handler found for response ID: {}", response.id);
        }
    }
};

// Global message registry for converting request/response to AsyncOp
ao::MessageRegistry<Response> g_requestRegistry;

// Convert the traditional request/response pattern to AsyncOp
ao::AsyncOp<Response> makeRequest(const std::string& operation, const std::string& data) {
    spdlog::info("Making request for operation: '{}', data: '{}'", operation, data);
    
    ao::AsyncOp<Response> result;
    auto promise = result.promise();
    
    // Register the async operation with the registry
    int64_t msgId = g_requestRegistry.registerMessage(promise, std::chrono::seconds(15));
    
    // Send the actual request
    NetworkSimulator::getInstance().sendRequest(msgId, operation, data);
    
    return result;
}

// Alternative: Traditional callback-based approach for comparison
void makeRequestTraditional(const std::string& operation, 
                          const std::string& data,
                          std::function<void(Response)> onSuccess,
                          std::function<void(ao::ErrorCode)> onError) {
    spdlog::info("Traditional approach: Making request for operation: '{}'", operation);
    
    int64_t msgId = ao::IdGen().generateId(); // Generate ID manually
    
    // Register the callback handler
    NetworkSimulator::getInstance().registerResponseHandler(msgId, 
        [onSuccess, onError, msgId](const Response& response) {
            if (response.success) {
                onSuccess(response);
            } else {
                ao::invoke_main([onError, msgId]() {
                    onError(ao::ErrorCode::NetworkError);
                });
            }
        });
    
    // Send the request
    NetworkSimulator::getInstance().sendRequest(msgId, operation, data);
}

// Example: Using AsyncOp for a sequence of requests
void demonstrateSequentialRequests() {
    spdlog::info("\n=== Sequential Requests Example ===");
    
    // Chain multiple requests together
    makeRequest("login", "user123")
        .then([](const Response& loginResp) {
            spdlog::info("Login successful: {}", loginResp.data);
            
            // Use login result to make next request
            return makeRequest("fetchProfile", loginResp.data);
        })
        .then([](const Response& profileResp) {
            spdlog::info("Profile fetched: {}", profileResp.data);
            
            // Make another request
            return makeRequest("updateSettings", profileResp.data);
        })
        .then([](const Response& settingsResp) {
            spdlog::info("Settings updated: {}", settingsResp.data);
        })
        .onError([](ao::ErrorCode err) {
            spdlog::error("Request sequence failed: {}", err);
        });
}

// Example: Parallel requests
void demonstrateParallelRequests() {
    spdlog::info("\n=== Parallel Requests Example ===");
    
    // Create multiple parallel requests
    std::vector<ao::AsyncOp<Response>> requests;
    
    for (int i = 1; i <= 3; ++i) {
        requests.push_back(makeRequest("operation" + std::to_string(i), 
                                    "data" + std::to_string(i)));
    }
    
    // Wait for all to complete
    ao::all(requests)
        .then([](const std::vector<Response>& responses) {
            spdlog::info("All {} requests completed:", responses.size());
            for (size_t i = 0; i < responses.size(); ++i) {
                spdlog::info("  Response {}: {}", i + 1, responses[i].data);
            }
        })
        .onError([](ao::ErrorCode err) {
            spdlog::error("Some requests failed: {}", err);
        });
}

// Example: Error recovery with fallback
void demonstrateErrorRecovery() {
    spdlog::info("\n=== Error Recovery Example ===");
    
    // This request has a chance of failing, with fallback to cache
    makeRequest("fetchFromServer", "importantData")
        .recover([](ao::ErrorCode err) -> Response {
            spdlog::warn("Server request failed: {}, falling back to cache", err);
            
            // Simulate getting data from cache
            Response cachedResponse;
            cachedResponse.id = -1;
            cachedResponse.success = true;
            cachedResponse.data = "Cached data (fallback)";
            cachedResponse.errorMessage = "";
            
            return cachedResponse;
        })
        .then([](const Response& response) {
            spdlog::info("Final response: {}", response.data);
        });
}

// Example: Comparing traditional vs AsyncOp approach
void demonstrateComparison() {
    spdlog::info("\n=== Traditional vs AsyncOp Comparison ===");
    
    // Traditional approach
    makeRequestTraditional("traditional", "data", 
        [](const Response& resp) {
            spdlog::info("Traditional success: {}", resp.data);
        },
        [](ao::ErrorCode err) {
            spdlog::error("Traditional error: {}", err);
        });
    
    // AsyncOp approach
    makeRequest("asyncop", "data")
        .then([](const Response& resp) {
            spdlog::info("AsyncOp success: {}", resp.data);
        })
        .onError([](ao::ErrorCode err) {
            spdlog::error("AsyncOp error: {}", err);
        });
}

int main() {
    spdlog::set_level(spdlog::level::debug);
    spdlog::info("Message Registry Example");
    
    // Demonstrate different patterns
    demonstrateSequentialRequests();
    demonstrateParallelRequests();
    demonstrateErrorRecovery();
    demonstrateComparison();
    
    // Keep the program running to allow async operations to complete
    for (int i = 0; i < 10; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        NetworkSimulator::getInstance().processResponses();
    }
    
    // Manually clear the registry before program termination to avoid
    // issues with logging during global destructor cleanup
    g_requestRegistry.clearAll();
    
    spdlog::info("Example completed");
    return 0;
}