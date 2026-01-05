/// SocketCAN Demo - Demonstrates SocketCAN link functionality
///
/// This example shows how to:
/// 1. Create a virtual CAN interface (vcan)
/// 2. Send and receive CAN frames through SocketCAN
/// 3. Interact with external CAN tools (candump, cansend)
///
/// Build with: HARDWARE=1 make build
/// Run: ./build/linux/x86_64/release/socketcan_demo
///
/// To test with external tools:
///   1. Run this demo (it creates vcan0)
///   2. In another terminal: candump vcan0
///   3. Or send frames: cansend vcan0 123#DEADBEEF

#ifdef HAS_HARDWARE

#include <chrono>
#include <cstring>
#include <echo/echo.hpp>
#include <iomanip>
#include <sstream>
#include <thread>
#include <wirebit/wirebit.hpp>

using namespace wirebit;

/// Format CAN frame for display
std::string format_can_frame(const can_frame &cf) {
    std::stringstream ss;
    bool is_extended = (cf.can_id & CAN_EFF_FLAG) != 0;
    uint32_t id = cf.can_id & (is_extended ? CAN_EFF_MASK : CAN_SFF_MASK);

    ss << "ID=0x" << std::hex << std::setfill('0');
    ss << std::setw(is_extended ? 8 : 3) << id;
    ss << std::dec << " [" << (int)cf.can_dlc << "] ";

    for (int i = 0; i < cf.can_dlc; i++) {
        ss << std::hex << std::setfill('0') << std::setw(2) << (int)cf.data[i];
        if (i < cf.can_dlc - 1)
            ss << " ";
    }

    if (is_extended)
        ss << " (EXT)";
    if (cf.can_id & CAN_RTR_FLAG)
        ss << " (RTR)";

    return ss.str();
}

int main() {
    echo::info("=== SocketCAN Demo ===").cyan().bold();

    // Create SocketCAN link with vcan0
    echo::info("Creating SocketCAN link on vcan0...");

    SocketCanConfig config{
        .interface_name = "vcan0",
        .create_if_missing = true,
        .destroy_on_close = false, // Keep interface for debugging with candump
    };

    auto result = SocketCanLink::create(config);
    if (!result.is_ok()) {
        echo::error("Failed to create SocketCAN link: ", result.error().message.c_str()).red();
        echo::info("");
        echo::info("Troubleshooting:").yellow();
        echo::info("  1. Make sure you have vcan module: sudo modprobe vcan").yellow();
        echo::info("  2. Check sudoers config for passwordless ip commands").yellow();
        echo::info("  3. Try creating interface manually:").yellow();
        echo::info("     sudo ip link add dev vcan0 type vcan").yellow();
        echo::info("     sudo ip link set vcan0 up").yellow();
        return 1;
    }

    auto &link = result.value();

    echo::info("╔════════════════════════════════════════════════════════════╗").green();
    echo::info("║  SocketCAN Link Created Successfully!                      ║").green();
    echo::info("╠════════════════════════════════════════════════════════════╣").green();
    echo::info("║  Interface: ", link.interface_name().c_str()).green();
    echo::info("║  Socket FD: ", link.socket_fd()).green();
    echo::info("║                                                            ║").green();
    echo::info("║  Monitor with: candump ", link.interface_name().c_str()).green();
    echo::info("║  Send with:    cansend ", link.interface_name().c_str(), " 123#DEADBEEF").green();
    echo::info("╚════════════════════════════════════════════════════════════╝").green();

    echo::info("");
    echo::info("Sending test CAN frames every 2 seconds...");
    echo::info("Run 'candump vcan0' in another terminal to see the frames.");
    echo::info("Run 'cansend vcan0 123#AABBCCDD' to send frames to this demo.");
    echo::info("Press Ctrl+C to exit.");
    echo::info("");

    // Demo loop - send test frames
    int frame_count = 0;
    uint32_t base_id = 0x100;

    while (true) {
        frame_count++;

        // Create a test CAN frame
        can_frame cf = {};
        cf.can_id = base_id + (frame_count % 16); // Vary ID from 0x100-0x10F
        cf.can_dlc = std::min(frame_count % 9, 8);

        // Fill data with pattern
        for (int i = 0; i < cf.can_dlc; i++) {
            cf.data[i] = static_cast<uint8_t>((frame_count * 0x11 + i) & 0xFF);
        }

        // Wrap in wirebit Frame
        Bytes payload(sizeof(can_frame));
        std::memcpy(payload.data(), &cf, sizeof(can_frame));
        Frame frame = make_frame(FrameType::CAN, std::move(payload), 1, 0);

        // Send through SocketCAN
        auto send_result = link.send(frame);
        if (send_result.is_ok()) {
            echo::info("[TX] ", format_can_frame(cf).c_str()).cyan();
        } else {
            echo::warn("[TX] Failed: ", send_result.error().message.c_str()).yellow();
        }

        // Try to receive any incoming frames
        for (int i = 0; i < 10; i++) { // Check for multiple frames
            auto recv_result = link.recv();
            if (recv_result.is_ok()) {
                Frame &received = recv_result.value();
                if (received.payload.size() == sizeof(can_frame)) {
                    can_frame received_cf;
                    std::memcpy(&received_cf, received.payload.data(), sizeof(can_frame));
                    echo::info("[RX] ", format_can_frame(received_cf).c_str()).magenta();
                }
            } else {
                break; // No more frames
            }
        }

        // Print stats periodically
        if (frame_count % 5 == 0) {
            echo::debug("Stats: TX=", link.stats().frames_sent, " RX=", link.stats().frames_received,
                        " errors=", link.stats().send_errors + link.stats().recv_errors);
        }

        std::this_thread::sleep_for(std::chrono::seconds(2));
    }

    return 0;
}

#else // HAS_HARDWARE

#include <echo/echo.hpp>

int main() {
    echo::error("This demo requires HAS_HARDWARE compile flag.").red();
    echo::info("Rebuild with: HARDWARE=1 make reconfig && HARDWARE=1 make build");
    return 1;
}

#endif // HAS_HARDWARE
