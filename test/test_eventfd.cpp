/**
 * @file test_eventfd.cpp
 * @brief Tests for eventfd notification system
 */

#include <echo/echo.hpp>
#include <wirebit/common/types.hpp>
#include <wirebit/shm/handshake.hpp>

#include <cassert>
#include <chrono>
#include <cstring>
#include <thread>
#include <unistd.h>

using namespace wirebit;

// Test helper: cleanup socket file
void cleanup_socket(const char *name) {
    String socket_path = String("/tmp/wirebit_") + String(name) + String(".sock");
    unlink(socket_path.c_str());
}

// Test 1: Basic eventfd creation and handshake
void test_eventfd_handshake() {
    echo::info("Test 1: Basic eventfd creation and handshake").cyan();

    const char *link_name = "test_handshake";
    cleanup_socket(link_name);

    EventfdPair server_fds;
    EventfdPair client_fds;

    // Server thread: create and send eventfds
    std::thread server_thread([&]() {
        auto result = create_and_send_eventfds(String(link_name));
        assert(result.is_ok());
        server_fds = std::move(result.value());

        assert(server_fds.a2b >= 0);
        assert(server_fds.b2a >= 0);
        echo::debug("Server: eventfds created - a2b=", server_fds.a2b, " b2a=", server_fds.b2a);
    });

    // Give server time to set up
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Client thread: receive eventfds
    std::thread client_thread([&]() {
        auto result = receive_eventfds(String(link_name));
        assert(result.is_ok());
        client_fds = std::move(result.value());

        assert(client_fds.a2b >= 0);
        assert(client_fds.b2a >= 0);
        echo::debug("Client: eventfds received - a2b=", client_fds.a2b, " b2a=", client_fds.b2a);
    });

    server_thread.join();
    client_thread.join();

    // Verify FDs are valid
    assert(server_fds.a2b >= 0);
    assert(server_fds.b2a >= 0);
    assert(client_fds.a2b >= 0);
    assert(client_fds.b2a >= 0);

    // Cleanup
    close(server_fds.a2b);
    close(server_fds.b2a);
    close(client_fds.a2b);
    close(client_fds.b2a);
    cleanup_socket(link_name);

    echo::info("✓ Test 1 passed").green();
}

// Test 2: Eventfd notification and waiting
void test_eventfd_notify_wait() {
    echo::info("Test 2: Eventfd notification and waiting").cyan();

    const char *link_name = "test_notify";
    cleanup_socket(link_name);

    EventfdPair server_fds;
    EventfdPair client_fds;
    bool notification_received = false;

    // Server thread
    std::thread server_thread([&]() {
        auto result = create_and_send_eventfds(String(link_name));
        assert(result.is_ok());
        server_fds = std::move(result.value());
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Client thread
    std::thread client_thread([&]() {
        auto result = receive_eventfds(String(link_name));
        assert(result.is_ok());
        client_fds = std::move(result.value());
    });

    server_thread.join();
    client_thread.join();

    // Test A->B notification
    std::thread waiter_thread([&]() {
        echo::debug("Waiter: waiting on a2b eventfd...");
        auto result = wait_eventfd(client_fds.a2b, 5000); // 5 second timeout
        assert(result.is_ok());
        notification_received = true;
        echo::debug("Waiter: notification received!");
    });

    // Give waiter time to start waiting
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Notify from server side
    echo::debug("Notifier: sending notification on a2b...");
    auto notify_result = notify_eventfd(server_fds.a2b);
    assert(notify_result.is_ok());

    waiter_thread.join();
    assert(notification_received);

    // Test B->A notification
    notification_received = false;

    std::thread waiter_thread2([&]() {
        echo::debug("Waiter: waiting on b2a eventfd...");
        auto result = wait_eventfd(server_fds.b2a, 5000);
        assert(result.is_ok());
        notification_received = true;
        echo::debug("Waiter: notification received!");
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    echo::debug("Notifier: sending notification on b2a...");
    notify_result = notify_eventfd(client_fds.b2a);
    assert(notify_result.is_ok());

    waiter_thread2.join();
    assert(notification_received);

    // Cleanup
    close(server_fds.a2b);
    close(server_fds.b2a);
    close(client_fds.a2b);
    close(client_fds.b2a);
    cleanup_socket(link_name);

    echo::info("✓ Test 2 passed").green();
}

// Test 3: Timeout behavior
void test_eventfd_timeout() {
    echo::info("Test 3: Eventfd timeout behavior").cyan();

    const char *link_name = "test_timeout";
    cleanup_socket(link_name);

    EventfdPair server_fds;
    EventfdPair client_fds;

    // Setup
    std::thread server_thread([&]() {
        auto result = create_and_send_eventfds(String(link_name));
        assert(result.is_ok());
        server_fds = std::move(result.value());
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    std::thread client_thread([&]() {
        auto result = receive_eventfds(String(link_name));
        assert(result.is_ok());
        client_fds = std::move(result.value());
    });

    server_thread.join();
    client_thread.join();

    // Wait with timeout (no notification sent)
    echo::debug("Testing timeout with 500ms wait...");
    auto start = std::chrono::steady_clock::now();
    auto result = wait_eventfd(client_fds.a2b, 500); // 500ms timeout
    auto end = std::chrono::steady_clock::now();
    auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

    // Should timeout
    assert(result.is_err());
    assert(elapsed_ms >= 450 && elapsed_ms <= 600); // Allow some tolerance
    echo::debug("Timeout occurred after ", elapsed_ms, "ms (expected ~500ms)");

    // Cleanup
    close(server_fds.a2b);
    close(server_fds.b2a);
    close(client_fds.a2b);
    close(client_fds.b2a);
    cleanup_socket(link_name);

    echo::info("✓ Test 3 passed").green();
}

// Test 4: Multiple notifications (semaphore behavior)
void test_eventfd_multiple_notifications() {
    echo::info("Test 4: Multiple notifications (semaphore behavior)").cyan();

    const char *link_name = "test_multiple";
    cleanup_socket(link_name);

    EventfdPair server_fds;
    EventfdPair client_fds;

    // Setup
    std::thread server_thread([&]() {
        auto result = create_and_send_eventfds(String(link_name));
        assert(result.is_ok());
        server_fds = std::move(result.value());
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    std::thread client_thread([&]() {
        auto result = receive_eventfds(String(link_name));
        assert(result.is_ok());
        client_fds = std::move(result.value());
    });

    server_thread.join();
    client_thread.join();

    // Send 3 notifications
    echo::debug("Sending 3 notifications...");
    for (int i = 0; i < 3; i++) {
        auto result = notify_eventfd(server_fds.a2b);
        assert(result.is_ok());
        echo::debug("Notification ", i + 1, " sent");
    }

    // Should be able to wait 3 times without blocking
    for (int i = 0; i < 3; i++) {
        auto result = wait_eventfd(client_fds.a2b, 100); // Short timeout
        assert(result.is_ok());
        echo::debug("Notification ", i + 1, " received");
    }

    // 4th wait should timeout (no more notifications)
    echo::debug("Testing 4th wait (should timeout)...");
    auto result = wait_eventfd(client_fds.a2b, 100);
    assert(result.is_err());
    echo::debug("4th wait timed out as expected");

    // Cleanup
    close(server_fds.a2b);
    close(server_fds.b2a);
    close(client_fds.a2b);
    close(client_fds.b2a);
    cleanup_socket(link_name);

    echo::info("✓ Test 4 passed").green();
}

// Test 5: Error handling - client connects before server
void test_eventfd_no_server() {
    echo::info("Test 5: Error handling - no server").cyan();

    const char *link_name = "test_no_server";
    cleanup_socket(link_name);

    // Try to connect without server
    echo::debug("Attempting to connect without server...");
    auto result = receive_eventfds(String(link_name));

    // Should fail
    assert(result.is_err());
    echo::debug("Connection failed as expected");

    cleanup_socket(link_name);

    echo::info("✓ Test 5 passed").green();
}

// Test 6: Bidirectional communication
void test_eventfd_bidirectional() {
    echo::info("Test 6: Bidirectional communication").cyan();

    const char *link_name = "test_bidir";
    cleanup_socket(link_name);

    EventfdPair server_fds;
    EventfdPair client_fds;
    int server_notifications = 0;
    int client_notifications = 0;

    // Setup
    std::thread server_thread([&]() {
        auto result = create_and_send_eventfds(String(link_name));
        assert(result.is_ok());
        server_fds = std::move(result.value());
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    std::thread client_thread([&]() {
        auto result = receive_eventfds(String(link_name));
        assert(result.is_ok());
        client_fds = std::move(result.value());
    });

    server_thread.join();
    client_thread.join();

    // Server waits for client notifications
    std::thread server_waiter([&]() {
        for (int i = 0; i < 3; i++) {
            auto result = wait_eventfd(server_fds.b2a, 5000);
            assert(result.is_ok());
            server_notifications++;
            echo::debug("Server received notification ", i + 1);
        }
    });

    // Client waits for server notifications
    std::thread client_waiter([&]() {
        for (int i = 0; i < 3; i++) {
            auto result = wait_eventfd(client_fds.a2b, 5000);
            assert(result.is_ok());
            client_notifications++;
            echo::debug("Client received notification ", i + 1);
        }
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Server sends to client
    std::thread server_notifier([&]() {
        for (int i = 0; i < 3; i++) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            auto result = notify_eventfd(server_fds.a2b);
            assert(result.is_ok());
            echo::debug("Server sent notification ", i + 1);
        }
    });

    // Client sends to server
    std::thread client_notifier([&]() {
        for (int i = 0; i < 3; i++) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            auto result = notify_eventfd(client_fds.b2a);
            assert(result.is_ok());
            echo::debug("Client sent notification ", i + 1);
        }
    });

    server_waiter.join();
    client_waiter.join();
    server_notifier.join();
    client_notifier.join();

    assert(server_notifications == 3);
    assert(client_notifications == 3);

    // Cleanup
    close(server_fds.a2b);
    close(server_fds.b2a);
    close(client_fds.a2b);
    close(client_fds.b2a);
    cleanup_socket(link_name);

    echo::info("✓ Test 6 passed").green();
}

int main() {
    echo::info("=== Eventfd Notification System Tests ===").bold().cyan();
    echo::info("");

    try {
        test_eventfd_handshake();
        test_eventfd_notify_wait();
        test_eventfd_timeout();
        test_eventfd_multiple_notifications();
        test_eventfd_no_server();
        test_eventfd_bidirectional();

        echo::info("");
        echo::info("=== All tests passed! ===").bold().green();
        return 0;
    } catch (const std::exception &e) {
        echo::error("Test failed with exception: ", e.what()).red();
        return 1;
    } catch (...) {
        echo::error("Test failed with unknown exception").red();
        return 1;
    }
}
