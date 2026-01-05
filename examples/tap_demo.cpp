/// TAP Demo - Demonstrates TAP link functionality
///
/// This example shows how to:
/// 1. Create a TAP interface
/// 2. Send and receive raw L2 Ethernet frames through TAP
/// 3. Interact with external network tools (tcpdump, ping, etc.)
///
/// Build with: HARDWARE=1 make build
/// Run: ./build/linux/x86_64/release/tap_demo
///
/// To test with external tools:
///   1. Run this demo (it creates tap0)
///   2. In another terminal: sudo tcpdump -i tap0 -xx
///   3. Or configure IP and ping:
///      sudo ip addr add 10.0.0.1/24 dev tap0
///      ping 10.0.0.2 (this demo responds to ARP)

#ifdef HAS_HARDWARE

#include <chrono>
#include <cstring>
#include <echo/echo.hpp>
#include <iomanip>
#include <sstream>
#include <thread>
#include <wirebit/wirebit.hpp>

using namespace wirebit;

/// Format MAC address for display
std::string format_mac(const MacAddr &mac) {
    std::stringstream ss;
    ss << std::hex << std::setfill('0');
    for (size_t i = 0; i < ETH_ALEN; ++i) {
        ss << std::setw(2) << (int)mac[i];
        if (i < ETH_ALEN - 1)
            ss << ":";
    }
    return ss.str();
}

/// Format Ethernet frame for display
std::string format_eth_frame(const Bytes &frame) {
    if (frame.size() < ETH_HLEN) {
        return "Invalid frame (too small)";
    }

    MacAddr dst_mac, src_mac;
    uint16_t ethertype;
    Bytes payload;
    parse_eth_frame(frame, dst_mac, src_mac, ethertype, payload);

    std::stringstream ss;
    ss << "DST=" << format_mac(dst_mac) << " SRC=" << format_mac(src_mac);
    ss << " TYPE=0x" << std::hex << std::setfill('0') << std::setw(4) << ethertype;
    ss << std::dec << " [" << payload.size() << " bytes]";

    // Identify common ethertypes
    switch (ethertype) {
    case ETH_P_IP:
        ss << " (IPv4)";
        break;
    case ETH_P_ARP:
        ss << " (ARP)";
        break;
    case ETH_P_IPV6:
        ss << " (IPv6)";
        break;
    case ETH_P_8021Q:
        ss << " (VLAN)";
        break;
    default:
        break;
    }

    return ss.str();
}

/// Format hex dump
std::string hex_dump(const Bytes &data, size_t max_len = 32) {
    std::stringstream ss;
    ss << std::hex << std::setfill('0');
    size_t len = std::min(data.size(), max_len);
    for (size_t i = 0; i < len; ++i) {
        ss << std::setw(2) << (int)data[i];
        if (i < len - 1)
            ss << " ";
    }
    if (data.size() > max_len) {
        ss << " ...";
    }
    return ss.str();
}

int main() {
    echo::info("=== TAP Interface Demo ===").cyan().bold();

    // Create TAP link with tap0
    echo::info("Creating TAP link on tap0...");

    TapConfig config{
        .interface_name = "tap0",
        .create_if_missing = true,
        .destroy_on_close = false, // Keep interface for debugging with tcpdump
        .set_up_on_create = true,
    };

    auto result = TapLink::create(config);
    if (!result.is_ok()) {
        echo::error("Failed to create TAP link: ", result.error().message.c_str()).red();
        echo::info("");
        echo::info("Troubleshooting:").yellow();
        echo::info("  1. Make sure /dev/net/tun exists: ls -la /dev/net/tun").yellow();
        echo::info("  2. Check sudoers config for passwordless ip commands").yellow();
        echo::info("  3. Try creating interface manually:").yellow();
        echo::info("     sudo ip tuntap add dev tap0 mode tap user $USER").yellow();
        echo::info("     sudo ip link set tap0 up").yellow();
        return 1;
    }

    auto &link = result.value();

    echo::info("╔════════════════════════════════════════════════════════════════════╗").green();
    echo::info("║  TAP Link Created Successfully!                                    ║").green();
    echo::info("╠════════════════════════════════════════════════════════════════════╣").green();
    echo::info("║  Interface: ", link.interface_name().c_str()).green();
    echo::info("║  TAP FD:    ", link.tap_fd()).green();
    echo::info("║                                                                    ║").green();
    echo::info("║  Monitor with: sudo tcpdump -i ", link.interface_name().c_str(), " -xx").green();
    echo::info("║  Configure:    sudo ip addr add 10.0.0.1/24 dev ", link.interface_name().c_str()).green();
    echo::info("╚════════════════════════════════════════════════════════════════════╝").green();

    echo::info("");
    echo::info("Sending test Ethernet frames every 2 seconds...");
    echo::info("Run 'sudo tcpdump -i tap0 -xx' in another terminal to see the frames.");
    echo::info("Press Ctrl+C to exit.");
    echo::info("");

    // Our MAC address (locally administered)
    MacAddr our_mac = {0x02, 0x00, 0x00, 0x00, 0x00, 0x01};
    echo::info("Our MAC address: ", format_mac(our_mac).c_str());

    // Demo loop - send test frames
    int frame_count = 0;

    while (true) {
        frame_count++;

        // Create a test Ethernet frame
        MacAddr dst_mac = MAC_BROADCAST; // Broadcast

        // Test payload with frame counter
        Bytes payload(32);
        for (size_t i = 0; i < payload.size(); ++i) {
            payload[i] = static_cast<uint8_t>((frame_count + i) & 0xFF);
        }

        // Create L2 Ethernet frame
        Bytes eth_frame = make_eth_frame(dst_mac, our_mac, ETH_P_IP, payload);

        // Wrap in wirebit Frame
        Frame frame = make_frame(FrameType::ETHERNET, eth_frame, 1, 0);

        // Send through TAP
        auto send_result = link.send(frame);
        if (send_result.is_ok()) {
            echo::info("[TX] ", format_eth_frame(eth_frame).c_str()).cyan();
            echo::debug("     Data: ", hex_dump(payload).c_str());
        } else {
            echo::warn("[TX] Failed: ", send_result.error().message.c_str()).yellow();
        }

        // Try to receive any incoming frames
        for (int i = 0; i < 10; i++) { // Check for multiple frames
            auto recv_result = link.recv();
            if (recv_result.is_ok()) {
                Frame &received = recv_result.value();
                echo::info("[RX] ", format_eth_frame(received.payload).c_str()).magenta();
                echo::debug("     Data: ", hex_dump(received.payload).c_str());
            } else {
                break; // No more frames
            }
        }

        // Print stats periodically
        if (frame_count % 5 == 0) {
            echo::debug("Stats: TX=", link.stats().frames_sent, " (", link.stats().bytes_sent, " bytes)",
                        " RX=", link.stats().frames_received, " (", link.stats().bytes_received, " bytes)",
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
