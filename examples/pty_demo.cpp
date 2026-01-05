/// PtyLink Demo - Demonstrates PTY (pseudo-terminal) link functionality
///
/// This example shows how to:
/// 1. Create a PTY link
/// 2. Send and receive frames through the PTY
/// 3. Connect external serial tools (minicom, picocom)
///
/// Build with: HARDWARE=1 make build
/// Run: ./build/linux/x86_64/release/pty_demo
///
/// To test with external tool:
///   1. Run this demo
///   2. Note the slave path printed (e.g., /dev/pts/3)
///   3. In another terminal: minicom -D /dev/pts/3

#ifndef NO_HARDWARE

#include <chrono>
#include <echo/echo.hpp>
#include <thread>
#include <wirebit/wirebit.hpp>

using namespace wirebit;

int main() {
    echo::info("=== PtyLink Demo ===").cyan().bold();

    // Create PTY link
    echo::info("Creating PTY link...");
    auto result = PtyLink::create();
    if (!result.is_ok()) {
        echo::error("Failed to create PTY: ", result.error().message.c_str()).red();
        return 1;
    }

    auto &pty = result.value();

    echo::info("╔════════════════════════════════════════════════════════════╗").green();
    echo::info("║  PTY Link Created Successfully!                            ║").green();
    echo::info("╠════════════════════════════════════════════════════════════╣").green();
    echo::info("║  Slave path: ", pty.slave_path().c_str()).green();
    echo::info("║                                                            ║").green();
    echo::info("║  Connect with: minicom -D ", pty.slave_path().c_str()).green();
    echo::info("║            or: picocom ", pty.slave_path().c_str()).green();
    echo::info("║            or: screen ", pty.slave_path().c_str(), " 115200").green();
    echo::info("╚════════════════════════════════════════════════════════════╝").green();

    echo::info("");
    echo::info("Sending test frames every 2 seconds...");
    echo::info("Connect to the slave path with a serial tool to see the data.");
    echo::info("Press Ctrl+C to exit.");
    echo::info("");

    // Demo loop - send test frames
    int frame_count = 0;
    while (true) {
        frame_count++;

        // Create a test frame with some data
        String msg = String("Hello from wirebit! Frame #") + String(std::to_string(frame_count).c_str());
        Bytes payload(msg.begin(), msg.end());

        Frame frame = make_frame(FrameType::SERIAL, payload, 1, 0);

        // Send through PTY
        auto send_result = pty.send(frame);
        if (send_result.is_ok()) {
            echo::info("[TX] Sent frame #", frame_count, " (", payload.size(), " bytes)").cyan();
        } else {
            echo::warn("[TX] Failed: ", send_result.error().message.c_str()).yellow();
        }

        // Try to receive any incoming data
        auto recv_result = pty.recv();
        if (recv_result.is_ok()) {
            Frame &received = recv_result.value();
            echo::info("[RX] Received frame: ", received.payload.size(), " bytes").magenta();

            // Print payload as string if printable
            if (!received.payload.empty()) {
                std::string data(received.payload.begin(), received.payload.end());
                echo::info("[RX] Data: ", data.c_str()).magenta();
            }
        }

        // Print stats
        echo::debug("Stats: TX=", pty.stats().frames_sent, " frames (", pty.stats().bytes_sent,
                    " bytes), RX=", pty.stats().frames_received, " frames (", pty.stats().bytes_received, " bytes)");

        std::this_thread::sleep_for(std::chrono::seconds(2));
    }

    return 0;
}

#else // NO_HARDWARE

#include <echo/echo.hpp>

int main() {
    echo::error("This demo requires hardware support (disabled with NO_HARDWARE).").red();
    echo::info("Rebuild without NO_HARDWARE flag: make reconfig && make build");
    return 1;
}

#endif // NO_HARDWARE
