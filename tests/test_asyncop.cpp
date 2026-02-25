#include "async_op.hpp"
#include <iostream>
#include <string>
#include <set>
#include <vector>
#ifdef ASYNC_USE_QT
# include <QCoreApplication>
# include <QTimer>
# include <QEventLoop>
# include <QDateTime>
# include <spdlog/spdlog.h>
#else
# include <glib.h>
#endif

// Include msg_registry for IdGen tests
#include "msg_registry.hpp"

// Global test counter
static int g_totalTests = 0;
static int g_failedTests = 0;
#ifdef ASYNC_USE_QT
// nothing in qt
#else
static GMainLoop* g_mainLoop = nullptr;
#endif

// ──────────────────────────────────────────────
// Test helpers
// ──────────────────────────────────────────────

static void runEventLoopFor(int timeout_ms) {
#ifdef ASYNC_USE_QT
    QEventLoop loop;
    QTimer::singleShot(timeout_ms, &loop, &QEventLoop::quit);
    loop.exec();
#else
    g_timeout_add(timeout_ms, [](gpointer) -> gboolean { 
        g_main_loop_quit(g_mainLoop);
        return G_SOURCE_REMOVE;
    }, nullptr);
    g_main_loop_run(g_mainLoop);
#endif
}

static void runTest(const std::string& testName, bool expected, bool actual) {
    g_totalTests++;
    std::cout << testName << ": ";
    if (expected == actual) {
        std::cout << "PASS" << std::endl;
    } else {
        std::cout << "FAIL (expected " << expected
                  << ", got " << actual << ")" << std::endl;
        g_failedTests++;
    }
}

template<typename T>
static void runValueTest(const std::string& testName,
                         const T& expected,
                         const T& actual)
{
    g_totalTests++;
    std::cout << testName << ": ";
    if (expected == actual) {
        std::cout << "PASS" << std::endl;
    } else {
        std::cout << "FAIL (expected [" << expected
                  << "], got [" << actual << "])" << std::endl;
        g_failedTests++;
    }
}

// ──────────────────────────────────────────────
// Simulated async operations
// ──────────────────────────────────────────────

static ao::AsyncOp<std::string> simulateNetworkRequest(const std::string& request, int delay_ms, bool should_fail = false) {
    ao::AsyncOp<std::string> result;
    auto result_state = result.m_promise;
    
    ao::add_timeout(std::chrono::milliseconds(delay_ms), [result_state, request, should_fail]() {
        if (should_fail) {
            result_state->rejectWith(ao::ErrorCode::NetworkError);
        } else {
            result_state->resolveWith("Response to: " + request);
        }
        return false;
    });

    return result;
}

static ao::AsyncOp<int> simulateComputation(int value, int delay_ms, bool should_fail = false)
{
    ao::AsyncOp<int> result;
    auto result_state = result.m_promise;
    
    ao::add_timeout(std::chrono::milliseconds(delay_ms), [result_state, value, should_fail]() {
        if (should_fail) {
            result_state->rejectWith(ao::ErrorCode::InvalidResponse);
        } else {
            result_state->resolveWith(value * 2);
        }
        return false;
    });

    return result;
}

// ══════════════════════════════════════════════
// BASIC OPERATION TESTS
// ══════════════════════════════════════════════

void testBasicAsyncOp()
{
    std::cout << "\n=== Testing Basic AsyncOp ===" << std::endl;
    
    bool resolved = false;
    std::string result_value;

    simulateNetworkRequest("test", 50)
        .then([&](std::string response) {
            resolved = true;
            result_value = response;
        });
    
    runEventLoopFor(100);
    
    runTest("AsyncOp resolved", true, resolved);
    runValueTest("AsyncOp value", std::string("Response to: test"), result_value);
}

void testChaining()
{
    std::cout << "\n=== Testing AsyncOp Chaining ===" << std::endl;
    
    bool chain_completed = false;
    int final_value = 0;

    simulateComputation(5, 50)
        .then([](int val) {
            return simulateComputation(val, 50);
        })
        .then([&](int val) {
            chain_completed = true;
            final_value = val;
        });
    
    runEventLoopFor(200);
    
    runTest("Chain completed", true, chain_completed);
    runValueTest("Chained computation result", 20, final_value);
}

void testChainingMixedTypes()
{
    std::cout << "\n=== Testing Chain Mixed Return Types ===" << std::endl;
    
    bool all_executed = false;
    std::string final_result;

    simulateComputation(10, 50)
        .then([](int val) {
            return simulateNetworkRequest("data" + std::to_string(val), 50);
        })
        .then([&](std::string str) {
            final_result = str;
            all_executed = true;
        });
    
    runEventLoopFor(200);
    
    runTest("Mixed type chain executed", true, all_executed);
    runValueTest("Mixed type final result", std::string("Response to: data20"), final_result);
}

// ══════════════════════════════════════════════
// ERROR HANDLING TESTS
// ══════════════════════════════════════════════

void testErrorHandling()
{
    std::cout << "\n=== Testing Error Handling ===" << std::endl;
    
    bool error_handled = false;
    ao::ErrorCode caught_error = ao::ErrorCode::None;

    simulateNetworkRequest("fail", 50, true)
        .then([](std::string) {
            // Should not be called
        })
        .onError([&](ao::ErrorCode err) {
            error_handled = true;
            caught_error = err;
        });
    
    runEventLoopFor(100);
    
    runTest("Error handled", true, error_handled);
    runTest("Correct error code", 
            ao::ErrorCode::NetworkError == caught_error,
            true);
}

void testErrorPropagation()
{
    std::cout << "\n=== Testing Error Propagation ===" << std::endl;
    
    bool error_propagated = false;
    int then_called = 0;

    simulateNetworkRequest("fail", 50, true)
        .then([&](std::string) {
            then_called++;
            return simulateComputation(10, 50);
        })
        .then([&](int) {
            then_called++;
        })
        .onError([&](ao::ErrorCode) {
            error_propagated = true;
        });
    
    runEventLoopFor(200);
    
    runTest("Error propagated through chain", true, error_propagated);
    runValueTest("Then callbacks not called", 0, then_called);
}

void testErrorInMiddle()
{
    std::cout << "\n=== Testing Error in Middle of Chain ===" << std::endl;
    
    bool error_caught = false;
    int callbacks_executed = 0;

    simulateComputation(5, 50)
        .then([&](int val) {
            callbacks_executed++;
            return simulateComputation(val, 50, true);
        })
        .then([&](int) {
            callbacks_executed++;
            return simulateNetworkRequest("test", 50);
        })
        .then([&](std::string) {
            callbacks_executed++;
        })
        .onError([&](ao::ErrorCode err) {
            error_caught = true;
        });
    
    runEventLoopFor(250);
    
    runTest("Error caught from middle", true, error_caught);
    runValueTest("Only callbacks before error executed", 1, callbacks_executed);
}

// ══════════════════════════════════════════════
// ERROR RECOVERY TESTS (NEW)
// ══════════════════════════════════════════════

void testOtherwise()
{
    std::cout << "\n=== Testing otherwise() Recovery ===" << std::endl;
    
    bool recovered = false;
    int final_value = 0;

    simulateComputation(10, 50, true)  // Will fail
        .otherwise([](ao::ErrorCode err) -> int {
            spdlog::debug("Recovering from error {}", static_cast<int>(err));
            return 999;  // Fallback value
        })
        .then([&](int value) {
            recovered = true;
            final_value = value;
        });
    
    runEventLoopFor(150);
    
    runTest("otherwise() recovered from error", true, recovered);
    runValueTest("Recovered with fallback value", 999, final_value);
}

void testOtherwiseWithAsyncOp()
{
    std::cout << "\n=== Testing otherwise() with AsyncOp Return ===" << std::endl;
    
    bool recovered = false;
    int final_value = 0;

    simulateComputation(10, 50, true)  // Will fail
        .otherwise([](ao::ErrorCode err) {
            spdlog::debug("Attempting async recovery");
            return simulateComputation(50, 50);  // Recovery operation
        })
        .then([&](int value) {
            recovered = true;
            final_value = value;
        });
    
    runEventLoopFor(200);
    
    runTest("otherwise() recovered with async op", true, recovered);
    runValueTest("Recovered with async result", 100, final_value);
}

void testOtherwiseRethrow()
{
    std::cout << "\n=== Testing otherwise() Re-throw ===" << std::endl;
    
    bool final_error_caught = false;
    ao::ErrorCode final_error = ao::ErrorCode::None;

    simulateComputation(10, 50, true)  // Will fail with InvalidResponse
        .otherwise([](ao::ErrorCode err) -> int {
            spdlog::debug("Recovery handler decides to re-throw");
            throw err;  // Re-throw the error
        })
        .then([](int value) {
            // Should not execute
        })
        .onError([&](ao::ErrorCode err) {
            final_error_caught = true;
            final_error = err;
        });
    
    runEventLoopFor(150);
    
    runTest("otherwise() re-throw caught", true, final_error_caught);
    runTest("Re-thrown error preserved",
            ao::ErrorCode::InvalidResponse == final_error,
            true);
}

void testOtherwiseSuccessPropagation()
{
    std::cout << "\n=== Testing otherwise() Success Propagation ===" << std::endl;
    
    bool success_propagated = false;
    int final_value = 0;

    simulateComputation(10, 50)  // Will succeed
        .otherwise([](ao::ErrorCode err) -> int {
            // Should NOT be called
            return 999;
        })
        .then([&](int value) {
            success_propagated = true;
            final_value = value;
        });
    
    runEventLoopFor(150);
    
    runTest("otherwise() propagated success", true, success_propagated);
    runValueTest("Success value unchanged", 20, final_value);
}

void testOrElse()
{
    std::cout << "\n=== Testing orElse() Convenience ===" << std::endl;
    
    bool recovered = false;
    int final_value = 0;

    simulateComputation(10, 50, true)  // Will fail
        .orElse(777, "Using fallback value")
        .then([&](int value) {
            recovered = true;
            final_value = value;
        });
    
    runEventLoopFor(150);
    
    runTest("orElse() recovered", true, recovered);
    runValueTest("orElse() fallback value", 777, final_value);
}

void testRecoverFrom()
{
    std::cout << "\n=== Testing recoverFrom() Selective Recovery ===" << std::endl;
    
    // Test 1: Recover from matching error
    bool recovered = false;
    int final_value = 0;

    simulateComputation(10, 50, true)  // Fails with InvalidResponse
        .recoverFrom(ao::ErrorCode::InvalidResponse, [](ao::ErrorCode) -> int {
            return 555;
        })
        .then([&](int value) {
            recovered = true;
            final_value = value;
        });
    
    runEventLoopFor(150);
    
    runTest("recoverFrom() matched error", true, recovered);
    runValueTest("recoverFrom() recovery value", 555, final_value);
    
    // Test 2: Don't recover from non-matching error
    bool error_rethrown = false;
    ao::ErrorCode caught_error = ao::ErrorCode::None;

    simulateNetworkRequest("fail", 50, true)  // Fails with NetworkError
        .recoverFrom(ao::ErrorCode::Timeout, [](ao::ErrorCode) -> std::string {
            return "recovered";  // Won't execute
        })
        .then([](std::string) {
            // Should not execute
        })
        .onError([&](ao::ErrorCode err) {
            error_rethrown = true;
            caught_error = err;
        });
    
    runEventLoopFor(150);
    
    runTest("recoverFrom() re-threw non-matching error", true, error_rethrown);
    runTest("Original error preserved",
            ao::ErrorCode::NetworkError == caught_error,
            true);
}

void testRecoverBranching()
{
    std::cout << "\n=== Testing recover() Branching Feature ===" << std::endl;

    // Test 1: Success path - recover() should NOT handle, but pass through
    bool first_then_called = false;
    bool error_path_called = false;
    bool second_then_called = false;
    int success_value = 0;

    simulateComputation(10, 50)  // Will succeed with value 20
        .then([&](int value) {
            first_then_called = true;
            success_value = value;
            // Don't return anything - this creates AsyncOp<void>
        })
        .recover([&](ao::ErrorCode) {
            // Should NOT be called - success doesn't need recovery
            error_path_called = true;
        })
        .then([&]() {
            // This should still be called via propagating success callback
            second_then_called = true;
        });

    runEventLoopFor(150);

    runTest("Branching: first then() called", true, first_then_called);
    runTest("Branching: recover() NOT called on success", false, error_path_called);
    runTest("Branching: second then() called via propagation", true, second_then_called);
    runValueTest("Branching: success value captured", 20, success_value);

    // Test 2: Error path - recover() SHOULD handle
    bool then_skipped = true;  // Assume true, set false if then() called
    bool recovery_called = false;
    bool recovery_then_called = false;

    simulateComputation(10, 50, true)  // Will fail
        .then([&](int) {
            // Should NOT be called
            then_skipped = false;
        })
        .recover([&](ao::ErrorCode) {
            recovery_called = true;
            // Recovery handler for AsyncOp<void> returns void
        })
        .then([&]() {
            recovery_then_called = true;
        });

    runEventLoopFor(150);

    runTest("Branching: then() skipped on error", true, then_skipped);
    runTest("Branching: recover() called on error", true, recovery_called);
    runTest("Branching: then() after recover() called", true, recovery_then_called);
}

void testNext()
{
    std::cout << "\n=== Testing next() (Both Paths, Same Type) ===" << std::endl;
    
    // Test success path
    bool success_path_executed = false;
    std::string success_result;
    
    simulateComputation(10, 50)  // Will succeed
        .next(
            [&](int value) -> std::string {
                success_path_executed = true;
                return "Success: " + std::to_string(value);
            },
            [&](ao::ErrorCode err) -> std::string {
                return "Error: " + std::to_string(static_cast<int>(err));
            }
        )
        .then([&](std::string result) {
            success_result = result;
        });
    
    runEventLoopFor(150);
    
    runTest("next() success path executed", true, success_path_executed);
    runValueTest("next() success result", std::string("Success: 20"), success_result);
}

void testNextErrorPath()
{
    std::cout << "\n=== Testing next() Error Path ===" << std::endl;
    
    bool error_path_executed = false;
    std::string error_result;
    
    simulateComputation(10, 50, true)  // Will fail
        .next(
            [](int value) -> std::string {
                return "Success: " + std::to_string(value);
            },
            [&](ao::ErrorCode err) -> std::string {
                error_path_executed = true;
                return "Error: " + std::to_string(static_cast<int>(err));
            }
        )
        .then([&](std::string result) {
            error_result = result;
        });
    
    runEventLoopFor(150);
    
    runTest("next() error path executed", true, error_path_executed);
    runValueTest("next() error result", 
                 std::string("Error: " + std::to_string(static_cast<int>(ao::ErrorCode::InvalidResponse))),
                 error_result);
}

void testNextWithAsyncOp()
{
    std::cout << "\n=== Testing next() with AsyncOp Returns ===" << std::endl;
    
    bool completed = false;
    int final_value = 0;
    
    simulateComputation(10, 50)  // Will succeed with 20
        .next(
            [](int value) {
                // Success path: double the value asynchronously
                return simulateComputation(value, 50);
            },
            [](ao::ErrorCode err) {
                // Error path: return default value asynchronously
                return simulateComputation(999, 50);
            }
        )
        .then([&](int value) {
            completed = true;
            final_value = value;
        });
    
    runEventLoopFor(250);
    
    runTest("next() with AsyncOp completed", true, completed);
    runValueTest("next() with AsyncOp result", 40, final_value);  // 20 * 2
}

void testNextErrorWithAsyncOp()
{
    std::cout << "\n=== Testing next() Error Path with AsyncOp ===" << std::endl;
    
    bool completed = false;
    int final_value = 0;
    
    simulateComputation(10, 50, true)  // Will fail
        .next(
            [](int value) {
                return simulateComputation(value, 50);
            },
            [](ao::ErrorCode err) {
                // Error path: return recovery value asynchronously
                return simulateComputation(999, 50);
            }
        )
        .then([&](int value) {
            completed = true;
            final_value = value;
        });
    
    runEventLoopFor(250);
    
    runTest("next() error with AsyncOp completed", true, completed);
    runValueTest("next() error with AsyncOp result", 1998, final_value);  // 999 * 2
}

void testNextConvergence()
{
    std::cout << "\n=== Testing next() Path Convergence ===" << std::endl;
    
    // Test that both paths can converge to same continuation
    int test1_result = 0;
    int test2_result = 0;
    
    // Success path
    simulateComputation(5, 50)
        .next(
            [](int value) -> int { return value * 10; },
            [](ao::ErrorCode err) -> int { return -1; }
        )
        .then([](int value) { return value + 100; })
        .then([&](int value) { test1_result = value; });
    
    // Error path
    simulateComputation(5, 50, true)
        .next(
            [](int value) -> int { return value * 10; },
            [](ao::ErrorCode err) -> int { return -1; }
        )
        .then([](int value) { return value + 100; })
        .then([&](int value) { test2_result = value; });
    
    runEventLoopFor(250);
    
    runValueTest("next() success path converged", 200, test1_result);  // (5*2)*10 + 100
    runValueTest("next() error path converged", 99, test2_result);     // -1 + 100
}

void testNextChaining()
{
    std::cout << "\n=== Testing next() in Chain ===" << std::endl;
    
    bool completed = false;
    std::string final_result;
    
    simulateComputation(10, 50)
        .then([](int value) {
            // First step succeeds
            return simulateNetworkRequest("step1", 50);
        })
        .next(
            [](std::string response) -> std::string {
                return response + " -> processed";
            },
            [](ao::ErrorCode err) -> std::string {
                return "recovered from error";
            }
        )
        .then([&](std::string result) {
            completed = true;
            final_result = result;
        });
    
    runEventLoopFor(250);
    
    runTest("next() in chain completed", true, completed);
    runValueTest("next() chain result", 
                 std::string("Response to: step1 -> processed"),
                 final_result);
}

// ══════════════════════════════════════════════
// TIMEOUT TESTS
// ══════════════════════════════════════════════

void testTimeout()
{
    std::cout << "\n=== Testing Timeout ===" << std::endl;
    
    bool timed_out = false;
    ao::ErrorCode error = ao::ErrorCode::None;

    simulateNetworkRequest("slow", 200)
        .timeout(std::chrono::milliseconds(100))
        .then([](std::string) {
            // Should not be called
        })
        .onError([&](ao::ErrorCode err) {
            timed_out = (err == ao::ErrorCode::Timeout);
            error = err;
        });
    
    runEventLoopFor(250);
    
    runTest("Timeout triggered", true, timed_out);
    runTest("Timeout error code", 
            ao::ErrorCode::Timeout == error,
            true);
}

void testTimeoutSuccess()
{
    std::cout << "\n=== Testing Timeout (Success Case) ===" << std::endl;
    
    bool completed = false;
    std::string result;

    simulateNetworkRequest("fast", 50)
        .timeout(std::chrono::milliseconds(200))
        .then([&](std::string response) {
            completed = true;
            result = response;
        });
    
    runEventLoopFor(250);
    
    runTest("Completed before timeout", true, completed);
    runValueTest("Result received", std::string("Response to: fast"), result);
}

// ══════════════════════════════════════════════
// RETRY TESTS
// ══════════════════════════════════════════════

void testRetry()
{
    std::cout << "\n=== Testing Retry with Backoff ===" << std::endl;
    
    int attempt_count = 0;
    bool succeeded = false;
    
    auto operation = [&]() {
        attempt_count++;
        bool should_fail = (attempt_count < 3);
        return simulateNetworkRequest("retry-test", 50, should_fail);
    };
    
    ao::retryWithBackoff<std::string>(
        operation,
        5,
        std::chrono::milliseconds(50)
    )
    .then([&](std::string response) {
        succeeded = true;
    });
    
    runEventLoopFor(500);
    
    runTest("Retry succeeded", true, succeeded);
    runValueTest("Retry attempt count", 3, attempt_count);
}

void testRetryMaxAttempts()
{
    std::cout << "\n=== Testing Retry Max Attempts ===" << std::endl;
    
    int attempt_count = 0;
    bool failed = false;
    ao::ErrorCode error = ao::ErrorCode::None;
    
    auto operation = [&]() {
        attempt_count++;
        return simulateNetworkRequest("always-fail", 50, true);
    };
    
    ao::retryWithBackoff<std::string>(
        operation,
        3,
        std::chrono::milliseconds(50)
    )
    .then([](std::string) {
        // Should not be called
    })
    .onError([&](ao::ErrorCode err) {
        failed = true;
        error = err;
    });
    
    runEventLoopFor(400);
    
    runTest("Retry failed after max attempts", true, failed);
    runValueTest("All attempts exhausted", 3, attempt_count);
    runTest("Max retries error code",
            ao::ErrorCode::MaxRetriesExceeded == error,
            true);
}

// ══════════════════════════════════════════════
// COLLECTION OPERATION TESTS
// ══════════════════════════════════════════════

void testForEach()
{
    std::cout << "\n=== Testing forEach (Sequential) ===" << std::endl;
    
    std::vector<int> items = {1, 2, 3, 4, 5};
    std::vector<int> results;
    bool completed = false;

    ao::forEach(items, [&](int item) {
        results.push_back(item * 2);
        return simulateComputation(item, 30);
    })
    .then([&]() {
        completed = true;
    });
    
    runEventLoopFor(300);
    
    runTest("forEach completed", true, completed);
    runValueTest("forEach processed all items", 5, static_cast<int>(results.size()));
}

void testForEachFailure()
{
    std::cout << "\n=== Testing forEach Failure (Stops on Error) ===" << std::endl;
    
    std::vector<int> items = {1, 2, 3, 4, 5};
    int processed_count = 0;
    bool error_caught = false;

    ao::forEach(items, [&](int item) {
        processed_count++;
        bool should_fail = (item == 3);
        return simulateComputation(item, 30, should_fail);
    })
    .then([&]() {
        // Should not execute
    })
    .onError([&](ao::ErrorCode err) {
        error_caught = true;
    });
    
    runEventLoopFor(300);
    
    runTest("forEach error caught", true, error_caught);
    runValueTest("forEach stopped at failure", 3, processed_count);
}

void testForEachSettled()
{
    std::cout << "\n=== Testing forEachSettled() (Process All, Return Failed) ===" << std::endl;
    
    std::vector<int> items = {1, 2, 3, 4, 5};
    int processed_count = 0;
    bool completed = false;
    std::vector<int> failed_items;

    ao::forEachSettled(items, [&](int item) {
        processed_count++;
        bool should_fail = (item == 2 || item == 4);
        return simulateComputation(item, 30, should_fail);
    })
    .then([&](std::vector<int> failed) {
        completed = true;
        failed_items = std::move(failed);
    });
    
    runEventLoopFor(300);
    
    runTest("forEachSettled() completed", true, completed);
    runValueTest("forEachSettled() processed all items", 5, processed_count);
    runValueTest("forEachSettled() returned 2 failed items", 2, static_cast<int>(failed_items.size()));
    
    if (failed_items.size() == 2) {
        runValueTest("First failed item", 2, failed_items[0]);
        runValueTest("Second failed item", 4, failed_items[1]);
    }
}

void testForEachSettledAllSuccess()
{
    std::cout << "\n=== Testing forEachSettled() (All Success) ===" << std::endl;
    
    std::vector<int> items = {1, 2, 3};
    bool completed = false;
    std::vector<int> failed_items;

    ao::forEachSettled(items, [](int item) {
        return simulateComputation(item, 30);
    })
    .then([&](std::vector<int> failed) {
        completed = true;
        failed_items = std::move(failed);
    });
    
    runEventLoopFor(200);
    
    runTest("forEachSettled() all success - completed", true, completed);
    runValueTest("forEachSettled() all success - no failures", 0, static_cast<int>(failed_items.size()));
}

void testAll()
{
    std::cout << "\n=== Testing all() (Parallel Success) ===" << std::endl;
    
    std::vector<ao::AsyncOp<int>> ops;
    ops.push_back(simulateComputation(1, 50));
    ops.push_back(simulateComputation(2, 100));
    ops.push_back(simulateComputation(3, 75));
    
    bool completed = false;
    std::vector<int> results;
    
    ao::all(std::move(ops))
        .then([&](std::vector<int> values) {
            completed = true;
            results = std::move(values);
        });
    
    runEventLoopFor(200);
    
    runTest("all() completed", true, completed);
    runValueTest("all() result count", 3, static_cast<int>(results.size()));
    
    if (results.size() == 3) {
        runValueTest("all() result[0]", 2, results[0]);
        runValueTest("all() result[1]", 4, results[1]);
        runValueTest("all() result[2]", 6, results[2]);
    }
}

void testAllFailure()
{
    std::cout << "\n=== Testing all() Failure (First Error Fails All) ===" << std::endl;
    
    std::vector<ao::AsyncOp<int>> ops;
    ops.push_back(simulateComputation(1, 50));
    ops.push_back(simulateComputation(2, 75, true));
    ops.push_back(simulateComputation(3, 100));
    
    bool completed = false;
    bool error_caught = false;
    
    ao::all(std::move(ops))
        .then([&](std::vector<int> values) {
            completed = true;
        })
        .onError([&](ao::ErrorCode err) {
            error_caught = true;
        });
    
    runEventLoopFor(200);
    
    runTest("all() error caught", true, error_caught);
    runTest("all() then not called", false, completed);
}

void testAny()
{
    std::cout << "\n=== Testing any() (First Success Wins) ===" << std::endl;
    
    std::vector<ao::AsyncOp<std::string>> ops;
    ops.push_back(simulateNetworkRequest("fail1", 50, true));
    ops.push_back(simulateNetworkRequest("success", 100));
    ops.push_back(simulateNetworkRequest("fail2", 75, true));
    
    bool completed = false;
    std::string result;
    
    ao::any(std::move(ops))
        .then([&](std::string value) {
            completed = true;
            result = value;
        });
    
    runEventLoopFor(200);
    
    runTest("any() completed on first success", true, completed);
    runValueTest("any() got success value", std::string("Response to: success"), result);
}

void testAnyAllFail()
{
    std::cout << "\n=== Testing any() All Fail ===" << std::endl;
    
    std::vector<ao::AsyncOp<std::string>> ops;
    ops.push_back(simulateNetworkRequest("fail1", 50, true));
    ops.push_back(simulateNetworkRequest("fail2", 75, true));
    ops.push_back(simulateNetworkRequest("fail3", 100, true));
    
    bool error_caught = false;
    bool completed = false;
    
    ao::any(std::move(ops))
        .then([&](std::string value) {
            completed = true;
        })
        .onError([&](ao::ErrorCode err) {
            error_caught = true;
        });
    
    runEventLoopFor(200);
    
    runTest("any() all fail - error caught", true, error_caught);
    runTest("any() all fail - then not called", false, completed);
}

void testRaceFirstSuccess()
{
    std::cout << "\n=== Testing race() (First Success) ===" << std::endl;
    
    std::vector<ao::AsyncOp<std::string>> ops;
    ops.push_back(simulateNetworkRequest("slow", 150));
    ops.push_back(simulateNetworkRequest("fast", 50));
    ops.push_back(simulateNetworkRequest("medium", 100));
    
    bool completed = false;
    std::string result;
    
    ao::race(std::move(ops))
        .then([&](std::string value) {
            completed = true;
            result = value;
        });
    
    runEventLoopFor(200);
    
    runTest("race() completed on first settled", true, completed);
    runValueTest("race() fastest success wins", std::string("Response to: fast"), result);
}

void testRaceFirstFailure()
{
    std::cout << "\n=== Testing race() (First Failure Settles) ===" << std::endl;
    
    std::vector<ao::AsyncOp<std::string>> ops;
    ops.push_back(simulateNetworkRequest("slow", 150));
    ops.push_back(simulateNetworkRequest("fail-fast", 50, true));
    ops.push_back(simulateNetworkRequest("medium", 100));
    
    bool error_caught = false;
    bool completed = false;
    
    ao::race(std::move(ops))
        .then([&](std::string value) {
            completed = true;
        })
        .onError([&](ao::ErrorCode err) {
            error_caught = true;
        });
    
    runEventLoopFor(200);
    
    runTest("race() first failure settled", true, error_caught);
    runTest("race() then not called on first failure", false, completed);
}

void testPollUntilSuccess()
{
    std::cout << "\n=== Testing pollUntil (Condition Met) ===" << std::endl;
    
    int poll_count = 0;
    bool completed = false;
    int final_value = 0;
    
    ao::pollUntil<int>(
        [&]() {
            poll_count++;
            return simulateComputation(poll_count * 10, 50);
        },
        [](int value) {
            return value >= 40;
        },
        5,
        std::chrono::milliseconds(100)
    )
    .then([&](int value) {
        completed = true;
        final_value = value;
    });
    
    runEventLoopFor(1000);
    
    runTest("pollUntil completed", true, completed);
    runValueTest("pollUntil poll count", 2, poll_count);
    runValueTest("pollUntil final value", 40, final_value);
}

void testPollUntilMaxAttempts()
{
    std::cout << "\n=== Testing pollUntil (Max Attempts) ===" << std::endl;
    
    int poll_count = 0;
    bool error_caught = false;
    ao::ErrorCode error = ao::ErrorCode::None;
    
    ao::pollUntil<int>(
        [&]() {
            poll_count++;
            return simulateComputation(5, 50);
        },
        [](int value) {
            return value >= 100;
        },
        3,
        std::chrono::milliseconds(100)
    )
    .then([&](int value) {
        // Should not execute
    })
    .onError([&](ao::ErrorCode err) {
        error_caught = true;
        error = err;
    });
    
    runEventLoopFor(800);
    
    runTest("pollUntil max attempts - error caught", true, error_caught);
    runValueTest("pollUntil max attempts - poll count", 3, poll_count);
    runTest("pollUntil max attempts - error code",
            ao::ErrorCode::MaxRetriesExceeded == error,
            true);
}

// ══════════════════════════════════════════════
// UTILITY TESTS
// ══════════════════════════════════════════════

void testIdGeneration()
{
    std::cout << "\n=== Testing ID Generation (IdGen) ===" << std::endl;

    ao::IdGen idGen;
    
    // Test 1: Basic ID generation and uniqueness
    std::set<int64_t> generatedIds;
    for (int i = 0; i < 100; ++i) {
        int64_t id = idGen.generateId();
        runTest("IdGen generates positive IDs", id > 0, true);
        runTest("IdGen generates unique IDs", generatedIds.insert(id).second, true);
    }
    
    // Test 2: Monotonic increase
    std::vector<int64_t> ids;
    for (int i = 0; i < 10; ++i) {
        ids.push_back(idGen.generateId());
    }
    
    bool isMonotonic = true;
    for (size_t i = 1; i < ids.size(); ++i) {
        if (ids[i] <= ids[i-1]) {
            isMonotonic = false;
            break;
        }
    }
    runTest("IdGen IDs are monotonic", isMonotonic, true);
    
    // Test 3: Extract functions work correctly
    int64_t sampleId = idGen.generateId();
    int64_t extractedTimestamp = ao::IdGen::extractTimestamp(sampleId);
    int32_t extractedCounter = ao::IdGen::extractCounter(sampleId);
    
    runTest("extractTimestamp works", extractedTimestamp > 0, true);
    runTest("extractCounter works", extractedCounter >= 0, true);
    
    // Test 4: Reconstruction consistency
    uint64_t reconstructed = (static_cast<uint64_t>(extractedTimestamp) << 22) | extractedCounter;
    runTest("ID reconstruction", static_cast<int64_t>(reconstructed) == sampleId, true);
}

void testDelay()
{
    std::cout << "\n=== Testing delay() ===" << std::endl;
    
    bool completed = false;
    auto start = std::chrono::steady_clock::now();
    std::chrono::milliseconds elapsed{0};

    ao::delay(std::chrono::milliseconds(100))
        .then([&]() {
            completed = true;
            auto end = std::chrono::steady_clock::now();
            elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
            spdlog::info("elapsed count is {}", elapsed.count());
        });
    
    runEventLoopFor(200);
    
    runTest("delay() completed", true, completed);
    runTest("delay() time elapsed", elapsed.count() >= 95, true);  // Allow 5ms tolerance for timing variations
}

void testDefer()
{
    std::cout << "\n=== Testing defer() ===" << std::endl;
    
    bool executed = false;
    int result = 0;
    
    ao::defer([]() {
        return 42;
    })
    .then([&](int value) {
        executed = true;
        result = value;
    });
    
    runEventLoopFor(100);
    
    runTest("defer() executed", true, executed);
    runValueTest("defer() result", 42, result);
}

void testDeferException()
{
    std::cout << "\n=== Testing defer() with Exception ===" << std::endl;
    
    bool error_caught = false;
    ao::ErrorCode error = ao::ErrorCode::None;
    
    ao::defer([]() -> int {
        throw std::runtime_error("Test exception");
        return 42;
    })
    .then([&](int value) {
        // Should not execute
    })
    .onError([&](ao::ErrorCode err) {
        error_caught = true;
        error = err;
    });
    
    runEventLoopFor(100);
    
    runTest("defer() exception - error caught", true, error_caught);
    runTest("defer() exception - error code",
            ao::ErrorCode::Exception == error,
            true);
}

// ══════════════════════════════════════════════
// FACTORY & STATE TESTS
// ══════════════════════════════════════════════

void testFactoryResolved()
{
    std::cout << "\n=== Testing Factory: resolved() ===" << std::endl;
    
    bool resolved_called = false;
    int resolved_value = 0;
    
    ao::AsyncOp<int>::resolved(123)
        .then([&](int value) {
            resolved_called = true;
            resolved_value = value;
        });
    
    runEventLoopFor(100);
    
    runTest("Resolved factory", true, resolved_called);
    runValueTest("Resolved value", 123, resolved_value);
}

void testFactoryRejected()
{
    std::cout << "\n=== Testing Factory: rejected() ===" << std::endl;
    
    bool rejected_called = false;
    ao::ErrorCode rejected_error = ao::ErrorCode::None;
    
    ao::AsyncOp<int>::rejected(ao::ErrorCode::NetworkError)
        .onError([&](ao::ErrorCode err) {
            rejected_called = true;
            rejected_error = err;
        });
    
    runEventLoopFor(100);
    
    runTest("Rejected factory", true, rejected_called);
    runTest("Rejected error code",
            ao::ErrorCode::NetworkError == rejected_error,
            true);
}

void testStateHelpers()
{
    std::cout << "\n=== Testing State Helper Methods ===" << std::endl;
    
    bool resolved_called = false;
    bool rejected_called = false;
    int final_value = 0;
    
    // Test isPending
    ao::AsyncOp<int> op1;
    runTest("State isPending initially", true, op1.m_promise->isPending());
    runTest("State not isResolved initially", false, op1.m_promise->isResolved());
    runTest("State not isRejected initially", false, op1.m_promise->isRejected());
    runTest("State not isSettled initially", false, op1.m_promise->isSettled());
    
    // Test resolveWith
    op1.then([&](int value) {
        resolved_called = true;
        final_value = value;
    });
    
    op1.m_promise->resolveWith(42);
    
    runEventLoopFor(50);
    
    runTest("resolveWith called callback", true, resolved_called);
    runValueTest("resolveWith set correct value", 42, final_value);
    runTest("State isResolved after resolve", true, op1.m_promise->isResolved());
    runTest("State isSettled after resolve", true, op1.m_promise->isSettled());
    
    // Test rejectWith
    ao::AsyncOp<int> op2;
    op2.onError([&](ao::ErrorCode err) {
        rejected_called = true;
    });
    
    op2.m_promise->rejectWith(ao::ErrorCode::NetworkError);
    
    runEventLoopFor(50);
    
    runTest("rejectWith called callback", true, rejected_called);
    runTest("State isRejected after reject", true, op2.m_promise->isRejected());
    runTest("State isSettled after reject", true, op2.m_promise->isSettled());
}

void testIdempotency()
{
    std::cout << "\n=== Testing Idempotency ===" << std::endl;
    
    int callback_count = 0;
    int final_value = 0;
    
    ao::AsyncOp<int> op;
    
    op.then([&](int value) {
        callback_count++;
        final_value = value;
    });
    
    op.resolve(10);
    op.resolve(20);  // Should be ignored
    op.reject(ao::ErrorCode::NetworkError);  // Should be ignored
    
    runEventLoopFor(100);
    
    runTest("Idempotency - callback once", true, callback_count == 1);
    runValueTest("First resolve value used", 10, final_value);
}

// ══════════════════════════════════════════════
// ADVANCED FEATURE TESTS
// ══════════════════════════════════════════════

void testTap()
{
    std::cout << "\n=== Testing tap() ===" << std::endl;
    
    bool tap_executed = false;
    int tap_value = 0;
    bool then_executed = false;
    int final_value = 0;
    
    simulateComputation(21, 50)
        .tap([&](int value) {
            tap_executed = true;
            tap_value = value;
        })
        .then([&](int value) {
            then_executed = true;
            final_value = value;
        });
    
    runEventLoopFor(150);
    
    runTest("tap() executed", true, tap_executed);
    runTest("then() after tap() executed", true, then_executed);
    runValueTest("tap() saw correct value", 42, tap_value);
    runValueTest("Value passed through tap unchanged", 42, final_value);
}

void testTapWithError()
{
    std::cout << "\n=== Testing tap() with Error Path ===" << std::endl;
    
    bool tap_executed = false;
    bool error_caught = false;
    
    simulateComputation(21, 50, true)
        .tap([&](int value) {
            tap_executed = true;
        })
        .then([](int value) {
            // Should NOT execute
        })
        .onError([&](ao::ErrorCode err) {
            error_caught = true;
        });
    
    runEventLoopFor(150);
    
    runTest("tap() not executed on error", false, tap_executed);
    runTest("Error caught after tap", true, error_caught);
}

void testFinally()
{
    std::cout << "\n=== Testing finally() (Success Path) ===" << std::endl;
    
    bool finally_executed = false;
    bool then_executed = false;
    std::string final_value;
    
    simulateNetworkRequest("test", 50)
        .finally([&]() {
            finally_executed = true;
        })
        .then([&](std::string response) {
            then_executed = true;
            final_value = response;
        });
    
    runEventLoopFor(150);
    
    runTest("finally() executed on success", true, finally_executed);
    runTest("then() executed after finally", true, then_executed);
    runValueTest("Value preserved through finally", 
                 std::string("Response to: test"), final_value);
}

void testFinallyOnError()
{
    std::cout << "\n=== Testing finally() (Error Path) ===" << std::endl;
    
    bool finally_executed = false;
    bool error_caught = false;
    ao::ErrorCode caught_error = ao::ErrorCode::None;
    
    simulateNetworkRequest("fail", 50, true)
        .finally([&]() {
            finally_executed = true;
        })
        .then([](std::string) {
            // Should NOT execute
        })
        .onError([&](ao::ErrorCode err) {
            error_caught = true;
            caught_error = err;
        });
    
    runEventLoopFor(150);
    
    runTest("finally() executed on error", true, finally_executed);
    runTest("Error caught after finally", true, error_caught);
    runTest("Error preserved through finally",
            ao::ErrorCode::NetworkError == caught_error,
            true);
}

void testAllSettled()
{
    std::cout << "\n=== Testing allSettled() (Mixed Results) ===" << std::endl;
    
    std::vector<ao::AsyncOp<int>> ops;
    ops.push_back(simulateComputation(1, 50));
    ops.push_back(simulateComputation(2, 75, true));
    ops.push_back(simulateComputation(3, 100));
    ops.push_back(simulateComputation(4, 60, true));
    ops.push_back(simulateComputation(5, 80));
    
    bool completed = false;
    std::vector<ao::SettledResult<int>> results;
    
    ao::allSettled(std::move(ops))
        .then([&](std::vector<ao::SettledResult<int>> settled) {
            completed = true;
            results = std::move(settled);
        });
    
    runEventLoopFor(200);
    
    runTest("allSettled() completed", true, completed);
    runValueTest("allSettled() returned all 5 results", 5, static_cast<int>(results.size()));
    
    if (results.size() == 5) {
        runTest("Result[0] fulfilled", true, results[0].isFulfilled());
        runValueTest("Result[0] value", 2, results[0].value);
        
        runTest("Result[1] rejected", true, results[1].isRejected());
        runTest("Result[1] error", 
                ao::ErrorCode::InvalidResponse == results[1].error,
                true);
        
        runTest("Result[2] fulfilled", true, results[2].isFulfilled());
        runValueTest("Result[2] value", 6, results[2].value);
        
        runTest("Result[3] rejected", true, results[3].isRejected());
        
        runTest("Result[4] fulfilled", true, results[4].isFulfilled());
        runValueTest("Result[4] value", 10, results[4].value);
    }
}

void testMap()
{
    std::cout << "\n=== Testing map() (Sequential) ===" << std::endl;
    
    std::vector<int> items = {1, 2, 3, 4, 5};
    bool completed = false;
    std::vector<int> results;
    
    ao::map(items, [](int item) {
        return simulateComputation(item, 30);
    })
    .then([&](std::vector<int> transformed) {
        completed = true;
        results = std::move(transformed);
    });
    
    runEventLoopFor(300);
    
    runTest("map() completed", true, completed);
    runValueTest("map() result count", 5, static_cast<int>(results.size()));
    
    if (results.size() == 5) {
        runValueTest("map() result[0]", 2, results[0]);
        runValueTest("map() result[4]", 10, results[4]);
    }
}

void testMapFailure()
{
    std::cout << "\n=== Testing map() Failure ===" << std::endl;
    
    std::vector<int> items = {1, 2, 3, 4, 5};
    bool error_caught = false;
    int results_before_error = 0;
    
    ao::map(items, [&results_before_error](int item) {
        bool should_fail = (item == 3);
        if (!should_fail) {
            results_before_error++;
        }
        return simulateComputation(item, 30, should_fail);
    })
    .then([](std::vector<int>) {
        // Should not execute
    })
    .onError([&](ao::ErrorCode err) {
        error_caught = true;
    });
    
    runEventLoopFor(300);
    
    runTest("map() error caught", true, error_caught);
    runValueTest("map() stopped at failure", 2, results_before_error);
}

void testMapParallel()
{
    std::cout << "\n=== Testing mapParallel() ===" << std::endl;
    
    std::vector<int> items = {1, 2, 3, 4, 5};
    bool completed = false;
    std::vector<int> results;
    auto start_time = std::chrono::steady_clock::now();
    std::chrono::milliseconds elapsed_ms{0};
    
    ao::mapParallel(items, [](int item) {
        return simulateComputation(item, 50);
    })
    .then([&](std::vector<int> transformed) {
        completed = true;
        results = std::move(transformed);
        auto end_time = std::chrono::steady_clock::now();
        elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    });
    
    runEventLoopFor(200);
    
    runTest("mapParallel() completed", true, completed);
    runValueTest("mapParallel() result count", 5, static_cast<int>(results.size()));
    runTest("mapParallel() faster than sequential", elapsed_ms.count() < 150, true);
    
    if (results.size() == 5) {
        runValueTest("mapParallel() result[0]", 2, results[0]);
        runValueTest("mapParallel() result[4]", 10, results[4]);
    }
}

void testMapSettled()
{
    std::cout << "\n=== Testing mapSettled() (Sequential with All Results) ===" << std::endl;
    
    std::vector<int> items = {1, 2, 3, 4, 5};
    bool completed = false;
    std::vector<ao::SettledResult<int>> results;
    
    ao::mapSettled(items, [](int item) {
        bool should_fail = (item == 2 || item == 4);
        return simulateComputation(item, 30, should_fail);
    })
    .then([&](std::vector<ao::SettledResult<int>> settled) {
        completed = true;
        results = std::move(settled);
    });
    
    runEventLoopFor(300);
    
    runTest("mapSettled() completed", true, completed);
    runValueTest("mapSettled() returned all 5 results", 5, static_cast<int>(results.size()));
    
    if (results.size() == 5) {
        runTest("Result[0] fulfilled", true, results[0].isFulfilled());
        runValueTest("Result[0] value", 2, results[0].value);
        
        runTest("Result[1] rejected", true, results[1].isRejected());
        runTest("Result[1] error", 
                ao::ErrorCode::InvalidResponse == results[1].error,
                true);
        
        runTest("Result[2] fulfilled", true, results[2].isFulfilled());
        runValueTest("Result[2] value", 6, results[2].value);
        
        runTest("Result[3] rejected", true, results[3].isRejected());
        
        runTest("Result[4] fulfilled", true, results[4].isFulfilled());
        runValueTest("Result[4] value", 10, results[4].value);
    }
}

void testMapSettledAllSuccess()
{
    std::cout << "\n=== Testing mapSettled() (All Success) ===" << std::endl;
    
    std::vector<int> items = {1, 2, 3};
    bool completed = false;
    std::vector<ao::SettledResult<int>> results;
    
    ao::mapSettled(items, [](int item) {
        return simulateComputation(item, 30);
    })
    .then([&](std::vector<ao::SettledResult<int>> settled) {
        completed = true;
        results = std::move(settled);
    });
    
    runEventLoopFor(200);
    
    runTest("mapSettled() all success - completed", true, completed);
    runValueTest("mapSettled() all success - result count", 3, static_cast<int>(results.size()));
    
    if (results.size() == 3) {
        runTest("All results fulfilled", 
                results[0].isFulfilled() && results[1].isFulfilled() && results[2].isFulfilled(),
                true);
        runValueTest("Result[0] value", 2, results[0].value);
        runValueTest("Result[1] value", 4, results[1].value);
        runValueTest("Result[2] value", 6, results[2].value);
    }
}

// ══════════════════════════════════════════════
// COMPLEX INTEGRATION TEST
// ══════════════════════════════════════════════

void testComplexScenario()
{
    std::cout << "\n=== Testing Complex Scenario ===" << std::endl;
    
    bool scenario_complete = false;
    std::string final_result;
    
    ao::retryWithBackoff<std::string>(
        []() {
            static int attempt = 0;
            attempt++;
            return simulateNetworkRequest("login", 50, attempt < 2);
        },
        3,
        std::chrono::milliseconds(50)
    )
    .timeout(std::chrono::seconds(2))
    .then([](std::string login_response) {
        return simulateNetworkRequest("fetch_data", 50);
    })
    .then([](std::string data) {
        return simulateComputation(42, 50);
    })
    .then([&](int processed) {
        scenario_complete = true;
        final_result = "Processed: " + std::to_string(processed);
    })
    .onError([&](ao::ErrorCode err) {
        spdlog::error("Complex scenario failed: {}", static_cast<int>(err));
    });
    
    runEventLoopFor(500);
    
    runTest("Complex scenario completed", true, scenario_complete);
    runValueTest("Complex scenario result", std::string("Processed: 84"), final_result);
}

// ══════════════════════════════════════════════
// MAIN TEST RUNNER
// ══════════════════════════════════════════════

int test_main_asyncop()
{
    std::cout << "Starting Comprehensive AsyncOp Unit Tests" << std::endl;
    std::cout << "==========================================" << std::endl;
    
    spdlog::set_level(spdlog::level::warn);
    
    g_totalTests = 0;
    g_failedTests = 0;
    
#ifdef ASYNC_USE_QT
    // nothing in qt
#else
    g_mainLoop = g_main_loop_new(nullptr, FALSE);
#endif

    try {
        // Basic operations
        testBasicAsyncOp();
        testChaining();
        testChainingMixedTypes();
        
        // Error handling
        testErrorHandling();
        testErrorPropagation();
        testErrorInMiddle();
        
        // Error recovery (NEW)
        testOtherwise();
        testOtherwiseWithAsyncOp();
        testOtherwiseRethrow();
        testOtherwiseSuccessPropagation();
        testOrElse();
        testRecoverFrom();
        testRecoverBranching();
        testNext();
        testNextErrorPath();
        testNextWithAsyncOp();
        testNextErrorWithAsyncOp();
        testNextConvergence();
        testNextChaining();
        
        // Timeout
        testTimeout();
        testTimeoutSuccess();
        
        // Retry
        testRetry();
        testRetryMaxAttempts();
        
        // Collections
        testForEach();
        testForEachFailure();
        testForEachSettled();
        testForEachSettledAllSuccess();
        testAll();
        testAllFailure();
        testAny();
        testAnyAllFail();
        testRaceFirstSuccess();
        testRaceFirstFailure();
        testPollUntilSuccess();
        testPollUntilMaxAttempts();
        
        // Utilities
        testIdGeneration();
        testDelay();
        testDefer();
        testDeferException();
        
        // Factory & State
        testFactoryResolved();
        testFactoryRejected();
        testStateHelpers();
        testIdempotency();
        
        // Advanced features
        testTap();
        testTapWithError();
        testFinally();
        testFinallyOnError();
        testAllSettled();
        testMap();
        testMapFailure();
        testMapParallel();
        testMapSettled();
        testMapSettledAllSuccess();
        
        // Integration
        testComplexScenario();
        
        std::cout << "\n==========================================" << std::endl;
        std::cout << "Test Summary:" << std::endl;
        std::cout << "  Total tests: " << g_totalTests << std::endl;
        std::cout << "  Passed: " << (g_totalTests - g_failedTests) << std::endl;
        std::cout << "  Failed: " << g_failedTests << std::endl;
        
        if (g_failedTests == 0) {
            std::cout << "\n✓ All tests passed successfully!" << std::endl;
        } else {
            std::cout << "\n✗ Some tests failed!" << std::endl;
        }
        
#ifdef ASYNC_USE_QT
        // nothing in qt
#else
        g_main_loop_unref(g_mainLoop);
#endif

        return (g_failedTests == 0) ? 0 : 1;
        
    } catch (const std::exception& e) {
        std::cerr << "\nTest suite failed with exception: "
                  << e.what() << std::endl;
#ifdef ASYNC_USE_QT
        // nothing in qt
#else
        g_main_loop_unref(g_mainLoop);
#endif
        return 1;
    }
}
