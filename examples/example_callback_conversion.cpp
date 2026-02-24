/**
 * @file example_callback_conversion.cpp
 * @brief Examples of converting callback-based workflows to AsyncOp
 * 
 * This example demonstrates how to convert traditional callback-based
 * async operations to the AsyncOp pattern.
 */

#include "async_op.hpp"
#include <iostream>
#include <functional>
#include <chrono>
#include <thread>
#include <spdlog/spdlog.h>

// Example 1: Converting a simple callback-based async function
// Traditional callback-based approach:
/*
void fetchUserAsync(int userId, std::function<void(User)> onSuccess, 
                    std::function<void(ErrorCode)> onError);
*/

// Converted to AsyncOp:
ao::AsyncOp<std::string> fetchUserAsync(int userId) {
    spdlog::info("Fetching user with ID: {}", userId);
    
    ao::AsyncOp<std::string> result;
    auto promise = result.promise();
    
    // Simulate async work in a separate thread
    std::thread([userId, promise]() {
        // Simulate network delay
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        // Simulate success or failure randomly
        if (userId > 0) {
            std::string userData = "User" + std::to_string(userId);
            
            // Resolve on main thread
            ao::invoke_main([promise, userData = std::move(userData)]() {
                promise->resolveWith(std::move(userData));
            });
        } else {
            // Resolve with error
            ao::invoke_main([promise]() {
                promise->rejectWith(ao::ErrorCode::InvalidResponse);
            });
        }
    }).detach();
    
    return result;
}

// Example 2: Converting a traditional callback-based async function with a completion callback
// Traditional callback-based approach:
/*
void sendHttpRequest(const HttpRequest& req, 
                     std::function<void(HttpResponse)> onComplete, 
                     std::function<void(ErrorCode)> onError);
*/

// Converted to AsyncOp:
ao::AsyncOp<std::string> sendHttpRequest(const std::string& url, const std::string& data) {
    spdlog::info("Sending HTTP request to: {}", url);
    
    ao::AsyncOp<std::string> result;
    auto promise = result.promise();
    
    // Simulate async HTTP request in a separate thread
    std::thread([url, data, promise]() {
        // Simulate network delay
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
        
        // Simulate success or failure
        if (url.find("invalid") == std::string::npos) {
            std::string response = "HTTP response from " + url + " with data: " + data;
            
            // Resolve on main thread (critical: always resolve on main thread!)
            ao::invoke_main([promise, response = std::move(response)]() {
                promise->resolveWith(std::move(response));
            });
        } else {
            // Resolve with error
            ao::invoke_main([promise]() {
                promise->rejectWith(ao::ErrorCode::NetworkError);
            });
        }
    }).detach();
    
    return result;
}

// Example 3: Converting a synchronous function to AsyncOp using defer
ao::AsyncOp<int> expensiveComputationSync(int value) {
    // Simulate expensive computation
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    return ao::AsyncOp<int>::resolved(value * value);
}

// Converted to async using defer
ao::AsyncOp<int> expensiveComputationAsync(int value) {
    return ao::defer([value]() {
        // This runs on the main event loop but simulates the computation
        // In real usage, this would be the actual computation or would
        // spawn a thread for CPU-intensive work
        // For demo purposes, just return the squared value directly
        return value * value;
    });
}

// Alternative approach: Actually defer to a worker thread
ao::AsyncOp<int> expensiveComputationAsyncReal(int value) {
    ao::AsyncOp<int> result;
    auto promise = result.promise();
    
    std::thread([value, promise]() {
        // Perform expensive computation on worker thread
        int computedValue = value * value * value;  // More intensive calculation
        
        // Marshal result back to main thread
        ao::invoke_main([promise, computedValue]() {
            promise->resolveWith(computedValue);
        });
    }).detach();
    
    return result;
}

// Example 4: Error handling and recovery patterns
void demonstrateErrorHandling() {
    spdlog::info("\n=== Demonstrating Error Handling ===");
    
    // Example of error recovery
    fetchUserAsync(-1)  // This will fail
        .recover([](ao::ErrorCode err) -> std::string {
            spdlog::warn("User fetch failed, using default user: {}", err);
            return "DefaultUser";
        })
        .then([](const std::string& user) {
            spdlog::info("Got user (recovered): {}", user);
        });
    
    // Example of conditional error recovery
    sendHttpRequest("https://api.example.com/data", "sample data")
        .recoverFrom(ao::ErrorCode::NetworkError, [](ao::ErrorCode err) -> std::string {
            spdlog::warn("Network error occurred, returning mock response");
            return "Mock response data";
        })
        .then([](const std::string& response) {
            spdlog::info("Received response: {}", response);
        })
        .onError([](ao::ErrorCode err) {
            spdlog::error("Request ultimately failed: {}", err);
        });
}

// Example 5: Chaining operations
void demonstrateChaining() {
    spdlog::info("\n=== Demonstrating Chaining ===");
    
    fetchUserAsync(123)
        .then([](const std::string& user) {
            spdlog::info("Step 1: Fetched user {}", user);
            // Return another AsyncOp to chain
            return sendHttpRequest("https://api.example.com/userdetails", user);
        })
        .then([](const std::string& response) {
            spdlog::info("Step 2: Got details {}", response);
            return response;
        })
        .then([](const std::string& details) {
            spdlog::info("Step 3: Processed details {}", details);
        })
        .onError([](ao::ErrorCode err) {
            spdlog::error("Chain failed: {}", err);
        });
}

int main() {
    spdlog::set_level(spdlog::level::debug);
    spdlog::info("AsyncOp Conversion Examples");
    
    // Run the examples
    demonstrateChaining();
    demonstrateErrorHandling();
    
    // Create a few async operations to show in action
    fetchUserAsync(42)
        .then([](const std::string& user) {
            spdlog::info("Successfully fetched: {}", user);
        })
        .onError([](ao::ErrorCode err) {
            spdlog::error("Failed to fetch user: {}", err);
        });
    
    sendHttpRequest("https://api.example.com/test", "test data")
        .then([](const std::string& response) {
            spdlog::info("Received: {}", response);
        })
        .onError([](ao::ErrorCode err) {
            spdlog::error("Request failed: {}", err);
        });
    
    expensiveComputationAsyncReal(5)
        .then([](int result) {
            spdlog::info("Computed result: {}", result);
        })
        .onError([](ao::ErrorCode err) {
            spdlog::error("Computation failed: {}", err);
        });
    
    // Wait for all operations to complete (in a real app, you'd use the event loop)
    std::this_thread::sleep_for(std::chrono::seconds(3));
    
    spdlog::info("Examples completed");
    return 0;
}