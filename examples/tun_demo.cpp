/// TUN Demo - Demonstrates TUN link functionality
///
/// This example shows how to:
/// 1. Create a TUN interface with IP address
/// 2. Send and receive raw L3 IP packets through TUN
/// 3. Interact with external network tools (tcpdump, ping, etc.)
///
/// Build with: HARDWARE=1 make build
/// Run: ./build/linux/x86_64/release/tun_demo
///
/// To test with external tools:
///   1. Run this demo (it creates tun0 with IP 10.100.0.1/24)
///   2. In another terminal: sudo tcpdump -i tun0 -xx
///   3. Or test with ping:
///      ping 10.100.0.1 (host responds via kernel)
///      ping 10.100.0.2 (this demo can respond to ICMP)

#ifdef HAS_HARDWARE

#include <chrono>
#include <cstring>
#include <echo/echo.hpp>
#include <iomanip>
#include <sstream>
#include <thread>
#include <wirebit/wirebit.hpp>

using namespace wirebit;

/// Format IP address for display
std::string format_ip(const uint8_t *ip) {
    std::stringstream ss;
    ss << (int)ip[0] << "." << (int)ip[1] << "." << (int)ip[2] << "." << (int)ip[3];
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

/// Format IP packet for display
std::string format_ip_packet(const Bytes &packet) {
    if (packet.size() < 20) {
        return "Invalid IP packet (too small)";
    }

    std::stringstream ss;

    uint8_t version = (packet[0] >> 4) & 0x0F;
    uint8_t ihl = packet[0] & 0x0F;
    uint16_t total_len = (packet[2] << 8) | packet[3];
    uint8_t protocol = packet[9];
    const uint8_t *src_ip = &packet[12];
    const uint8_t *dst_ip = &packet[16];

    ss << "IPv" << (int)version;
    ss << " SRC=" << format_ip(src_ip);
    ss << " DST=" << format_ip(dst_ip);
    ss << " LEN=" << total_len;
    ss << " PROTO=";

    switch (protocol) {
    case 1:
        ss << "ICMP";
        break;
    case 6:
        ss << "TCP";
        break;
    case 17:
        ss << "UDP";
        break;
    default:
        ss << (int)protocol;
        break;
    }

    // For ICMP, show type
    if (protocol == 1 && packet.size() > (size_t)(ihl * 4)) {
        uint8_t icmp_type = packet[ihl * 4];
        uint8_t icmp_code = packet[ihl * 4 + 1];
        ss << " (type=" << (int)icmp_type << " code=" << (int)icmp_code << ")";
    }

    return ss.str();
}

/// Create ICMP echo reply from request
Bytes create_icmp_reply(const Bytes &request) {
    if (request.size() < 28) { // IP header (20) + ICMP header (8)
        return {};
    }

    uint8_t protocol = request[9];
    if (protocol != 1) { // Not ICMP
        return {};
    }

    uint8_t ihl = request[0] & 0x0F;
    size_t ip_hdr_len = ihl * 4;
    if (request.size() < ip_hdr_len + 8) {
        return {};
    }

    uint8_t icmp_type = request[ip_hdr_len];
    if (icmp_type != 8) { // Not echo request
        return {};
    }

    // Create reply (copy request and modify)
    Bytes reply = request;

    // Swap source and destination IP
    for (int i = 0; i < 4; ++i) {
        std::swap(reply[12 + i], reply[16 + i]);
    }

    // Set ICMP type to 0 (echo reply)
    reply[ip_hdr_len] = 0;

    // Recalculate ICMP checksum
    // First zero out old checksum
    reply[ip_hdr_len + 2] = 0;
    reply[ip_hdr_len + 3] = 0;

    // Calculate new checksum over ICMP header + data
    uint32_t sum = 0;
    for (size_t i = ip_hdr_len; i < reply.size(); i += 2) {
        uint16_t word = reply[i] << 8;
        if (i + 1 < reply.size()) {
            word |= reply[i + 1];
        }
        sum += word;
    }
    while (sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }
    uint16_t checksum = ~sum;
    reply[ip_hdr_len + 2] = (checksum >> 8) & 0xFF;
    reply[ip_hdr_len + 3] = checksum & 0xFF;

    // Recalculate IP header checksum
    reply[10] = 0;
    reply[11] = 0;
    sum = 0;
    for (size_t i = 0; i < ip_hdr_len; i += 2) {
        uint16_t word = (reply[i] << 8) | reply[i + 1];
        sum += word;
    }
    while (sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }
    checksum = ~sum;
    reply[10] = (checksum >> 8) & 0xFF;
    reply[11] = checksum & 0xFF;

    return reply;
}

int main() {
    echo::info("=== TUN Interface Demo ===").cyan().bold();

    // Create TUN link with tun0 and IP address
    echo::info("Creating TUN link on tun0 with IP 10.100.0.1/24...");

    TunConfig config{
        .interface_name = "tun0",
        .create_if_missing = true,
        .destroy_on_close = false, // Keep interface for debugging with tcpdump
        .set_up_on_create = true,
        .ip_address = "10.100.0.1/24",
    };

    auto result = TunLink::create(config);
    if (!result.is_ok()) {
        echo::error("Failed to create TUN link: ", result.error().message.c_str()).red();
        echo::info("");
        echo::info("Troubleshooting:").yellow();
        echo::info("  1. Make sure /dev/net/tun exists: ls -la /dev/net/tun").yellow();
        echo::info("  2. Check sudoers config for passwordless ip commands").yellow();
        echo::info("  3. Try creating interface manually:").yellow();
        echo::info("     sudo ip tuntap add dev tun0 mode tun user $USER").yellow();
        echo::info("     sudo ip addr add 10.100.0.1/24 dev tun0").yellow();
        echo::info("     sudo ip link set tun0 up").yellow();
        return 1;
    }

    auto &link = result.value();

    echo::info("╔════════════════════════════════════════════════════════════════════╗").green();
    echo::info("║  TUN Link Created Successfully!                                    ║").green();
    echo::info("╠════════════════════════════════════════════════════════════════════╣").green();
    echo::info("║  Interface: ", link.interface_name().c_str()).green();
    echo::info("║  TUN FD:    ", link.tun_fd()).green();
    echo::info("║  IP Addr:   10.100.0.1/24").green();
    echo::info("║                                                                    ║").green();
    echo::info("║  Monitor with: sudo tcpdump -i ", link.interface_name().c_str(), " -xx").green();
    echo::info("║  Test ping:    ping 10.100.0.2 (this demo responds)").green();
    echo::info("╚════════════════════════════════════════════════════════════════════╝").green();

    echo::info("");
    echo::info("Listening for IP packets and responding to ICMP echo requests...");
    echo::info("Run 'ping 10.100.0.2' in another terminal.");
    echo::info("Press Ctrl+C to exit.");
    echo::info("");

    // Demo loop - receive and respond to packets
    int packet_count = 0;

    while (true) {
        // Try to receive incoming packets
        auto recv_result = link.recv();
        if (recv_result.is_ok()) {
            Frame &received = recv_result.value();
            packet_count++;

            echo::info("[RX #", packet_count, "] ", format_ip_packet(received.payload).c_str()).magenta();
            echo::debug("     Data: ", hex_dump(received.payload).c_str());

            // Try to create ICMP reply
            Bytes reply = create_icmp_reply(received.payload);
            if (!reply.empty()) {
                Frame reply_frame = make_frame(FrameType::IP, std::move(reply), 1, 0);
                auto send_result = link.send(reply_frame);
                if (send_result.is_ok()) {
                    echo::info("[TX] ICMP Echo Reply sent").cyan();
                } else {
                    echo::warn("[TX] Failed: ", send_result.error().message.c_str()).yellow();
                }
            }
        }

        // Print stats periodically
        if (packet_count > 0 && packet_count % 10 == 0) {
            echo::debug("Stats: TX=", link.stats().packets_sent, " (", link.stats().bytes_sent, " bytes)",
                        " RX=", link.stats().packets_received, " (", link.stats().bytes_received, " bytes)",
                        " errors=", link.stats().send_errors + link.stats().recv_errors);
        }

        // Small sleep to avoid busy loop
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
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
