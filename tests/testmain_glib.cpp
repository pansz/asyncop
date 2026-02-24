#include <glib.h>
#include "async_op.hpp"

int test_main_asyncop();

void runTests()
{
    test_main_asyncop();
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
        return false;  // G_SOURCE_REMOVE
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
        return false;  // G_SOURCE_REMOVE
    });

    return result;
}

int main(int argc, char *argv[])
{
    GMainLoop *loop = g_main_loop_new(NULL, FALSE);

    auto asop = simulateNetworkRequest("request1", 50);

    asop.then([loop](std::string response) {
            runTests();
            g_main_loop_quit(loop);
        })
        .onError([loop](ao::ErrorCode ec) { 
            spdlog::error("error place {}", ec);
            g_main_loop_quit(loop);
        });

    // Run the GLib main loop
    g_main_loop_run(loop);

    // Cleanup
    g_main_loop_unref(loop);

    return 0;
}