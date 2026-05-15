/**
 * @file test_msgregistry_duplicate.cpp
 * @brief Test duplicate message ID registration with different states
 *
 * This test verifies behavior when attempting to register duplicate
 * message IDs with different AsyncOp states - a scenario that could
 * cause crashes in production if not handled properly.
 */

#include "async_op.hpp"
#include "msg_registry.hpp"
#include <iostream>
#include <cassert>
#include <thread>
#include <unordered_set>
#include <vector>

#ifdef ASYNC_USE_QT
# include <QCoreApplication>
# include <QTimer>
#else
# include <glib.h>
#endif

using namespace ao;

// Test 1: Sequential duplicate registration (should throw exception)
void testSequentialDuplicate() {
    std::cout << "\n=== Test 1: Sequential Duplicate Registration ===" << std::endl;

    MsgRegistry<int> registry;

    AsyncOp<int> op1;
    AsyncOp<int> op2;

    // First registration should succeed
    int64_t id1 = registry.registerMessage(op1.promise(), std::chrono::seconds(5));
    std::cout << "First registration succeeded, ID: " << IdGen::formatId(id1) << std::endl;

    // Force same ID by using the same timestamp (simulate race condition)
    // In real scenario, this could happen due to:
    // 1. Clock skew
    // 2. Counter wrapping (unlikely with 21 bits)
    // 3. Bug in ID generation
    // 4. Memory corruption

    // Try to register with same ID manually (simulating duplicate)
    try {
        registry.registerMessage(id1, op2.promise(), std::chrono::seconds(5));
        std::cout << "ERROR: Second registration should have thrown!" << std::endl;
        assert(false && "Should have thrown exception");
    } catch (const std::runtime_error& e) {
        std::cout << "Caught expected exception: " << e.what() << std::endl;
        std::cout << "PASS: Exception thrown for duplicate ID" << std::endl;
    }
}

// Test 2: Rapid concurrent registrations (stress test for race conditions)
// Qt backend: skipped because add_timeout() creates QTimer(qApp) which requires QThread workers.
// MsgRegistry thread-safety is validated by other tests; this stress test focuses on IdGen
// concurrency which is backend-agnostic.
void testConcurrentRegistrations() {
    std::cout << "\n=== Test 2: Concurrent Registration Stress Test ===" << std::endl;

#ifdef ASYNC_USE_QT
    std::cout << "SKIPPED: Qt backend cannot create QTimer from std::thread workers." << std::endl;
    std::cout << "         MsgRegistry concurrency is validated via GLib backend." << std::endl;
#else
    MsgRegistry<int> registry;
    std::vector<std::thread> threads;
    std::atomic<int> success_count{0};
    std::atomic<int> exception_count{0};
    std::atomic<int> unknown_failure{0};

    const int num_threads = 100;
    const int ops_per_thread = 1000;

    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back([&registry, &success_count, &exception_count, &unknown_failure]() {
            for (int i = 0; i < ops_per_thread; ++i) {
                AsyncOp<int> op;
                try {
                    int64_t id = registry.registerMessage(op.promise(), std::chrono::seconds(30));
                    success_count.fetch_add(1, std::memory_order_relaxed);

                    // Immediately cancel to clean up
                    registry.cancelMessage(id);
                } catch (const std::runtime_error& e) {
                    exception_count.fetch_add(1, std::memory_order_relaxed);
                } catch (...) {
                    unknown_failure.fetch_add(1, std::memory_order_relaxed);
                }
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    std::cout << "Successful registrations: " << success_count.load() << std::endl;
    std::cout << "Exceptions (duplicate IDs): " << exception_count.load() << std::endl;
    std::cout << "Unknown failures: " << unknown_failure.load() << std::endl;
    std::cout << "Pending messages: " << registry.pendingCount() << std::endl;

    if (exception_count.load() > 0) {
        std::cout << "WARNING: Duplicate IDs detected under concurrent load!" << std::endl;
    }

    // Clean up
    registry.clearAll();
#endif
}

// Test 3: Same timestamp, different counter values
void testIdGenerationUniqueness() {
    std::cout << "\n=== Test 3: ID Generation Uniqueness ===" << std::endl;

    IdGen gen;
    const int num_ids = 100000;
    std::unordered_set<int64_t> ids;
    ids.reserve(num_ids);

    int duplicates = 0;
    for (int i = 0; i < num_ids; ++i) {
        int64_t id = gen.generateId();
        if (ids.find(id) != ids.end()) {
            duplicates++;
            std::cout << "Duplicate ID detected: " << id << " at iteration " << i << std::endl;
        }
        ids.insert(id);
    }

    std::cout << "Generated " << num_ids << " IDs" << std::endl;
    std::cout << "Unique IDs: " << ids.size() << std::endl;
    std::cout << "Duplicates: " << duplicates << std::endl;

    if (duplicates == 0) {
        std::cout << "PASS: All IDs are unique" << std::endl;
    } else {
        std::cout << "FAIL: Duplicate IDs were generated!" << std::endl;
    }
}

// Test 4: Registry with different state objects (simulating production scenario)
void testDifferentStateObjects() {
    std::cout << "\n=== Test 4: Different State Objects with Same ID ===" << std::endl;

    // This simulates what might happen in production:
    // - Two different operations somehow get the same ID
    // - Could be due to memory corruption, race condition, or ID generator bug

    MsgRegistry<std::string> registry;

    AsyncOp<std::string> state1;
    AsyncOp<std::string> state2;

    // Register first state
    int64_t id = registry.registerMessage(state1.promise(), std::chrono::seconds(5));
    std::cout << "Registered state1 with ID: " << IdGen::formatId(id) << std::endl;

    // Verify state1 is pending
    assert(registry.isPending(id));
    std::cout << "Verified: state1 is pending" << std::endl;

    // Now simulate what might happen if there's a bug:
    // Another part of the code tries to register state2 with the same ID
    // This should NOT crash - should throw exception
    bool exception_thrown = false;
    try {
        registry.registerMessage(id, state2.promise(), std::chrono::seconds(5));
    } catch (const std::exception& e) {
        exception_thrown = true;
        std::cout << "Exception caught: " << e.what() << std::endl;
    }

    if (exception_thrown) {
        std::cout << "PASS: Exception thrown, no crash" << std::endl;
    } else {
        std::cout << "FAIL: No exception thrown - this could cause undefined behavior!" << std::endl;
    }

    // Verify state1 is still the one registered (not corrupted)
    assert(registry.isPending(id));
    std::cout << "Verified: Original state1 still pending (not corrupted)" << std::endl;

    // Clean up
    registry.cancelMessage(id);
}

// Test 5: Message handling after duplicate attempt
void testMessageHandlingAfterDuplicateAttempt() {
    std::cout << "\n=== Test 5: Message Handling After Duplicate Attempt ===" << std::endl;

    MsgRegistry<int> registry;
    AsyncOp<int> original_state;
    bool original_resolved = false;
    int resolved_value = 0;

    // Set up callback on original state
    original_state.then([&original_resolved, &resolved_value](int value) {
        original_resolved = true;
        resolved_value = value;
        std::cout << "Original state resolved with value: " << value << std::endl;
        return value;
    });

    // Register original state
    int64_t id = registry.registerMessage(original_state.promise(), std::chrono::seconds(5));
    std::cout << "Registered original state with ID: " << id << std::endl;

    // Try to register duplicate (should fail)
    AsyncOp<int> duplicate_state;
    try {
        registry.registerMessage(id, duplicate_state.promise(), std::chrono::seconds(5));
    } catch (...) {
        std::cout << "Duplicate registration rejected (expected)" << std::endl;
    }

    // Now handle response for the original ID
    bool handled = registry.handleResponse(id, 42);
    std::cout << "handleResponse returned: " << (handled ? "true" : "false") << std::endl;

    assert(handled && "Should have handled the response");

    // Run event loop to process callbacks
#ifdef ASYNC_USE_QT
    QEventLoop loop;
    QTimer::singleShot(100, &loop, &QEventLoop::quit);
    loop.exec();
#else
    GMainLoop* loop = g_main_loop_new(nullptr, FALSE);
    g_timeout_add(100, [](gpointer user_data) -> gboolean {
        g_main_loop_quit(static_cast<GMainLoop*>(user_data));
        return G_SOURCE_REMOVE;
    }, loop);
    g_main_loop_run(loop);
    g_main_loop_unref(loop);
#endif

    assert(original_resolved && "Original state should have been resolved");
    assert(resolved_value == 42 && "Should have resolved with correct value");

    std::cout << "PASS: Original state correctly resolved after duplicate attempt" << std::endl;
}

int main(int argc, char* argv[]) {
    std::cout << "=== MessageRegistry Duplicate ID Tests ===" << std::endl;
    std::cout << "Testing behavior when duplicate IDs are registered with different states" << std::endl;

#ifdef ASYNC_USE_QT
    QCoreApplication app(argc, argv);
#endif

    try {
        testSequentialDuplicate();
        testDifferentStateObjects();
        testMessageHandlingAfterDuplicateAttempt();
        testIdGenerationUniqueness();
        testConcurrentRegistrations();

        std::cout << "\n=== All Tests Completed ===" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Test failed with exception: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
