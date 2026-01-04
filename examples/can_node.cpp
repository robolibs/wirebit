/// @file can_node.cpp
/// @brief CAN node client - standalone application for testing CAN bus hub
///
/// This program connects to a CAN bus hub and sends/receives CAN frames.
/// It demonstrates how to use the wirebit library for multi-process CAN communication.
///
/// Usage:
///   ./can_node <node_id> <mode> [can_id] [data]
///
/// Modes:
///   send    - Send a CAN frame and exit
///   recv    - Receive CAN frames (blocking)
///   pingpong - Send a frame and wait for response
///
/// Examples:
///   ./can_node 0 send 0x123 "01 02 03 04"
///   ./can_node 1 recv
///   ./can_node 0 pingpong 0x100

#include <cstdlib>
#include <cstring>
#include <echo/echo.hpp>
#include <iostream>
#include <sstream>
#include <wirebit/wirebit.hpp>

using namespace wirebit;

/// Parse hex data string like "01 02 03 04" into bytes
Vector<uint8_t> parse_hex_data(const char *str) {
    Vector<uint8_t> data;
    std::istringstream iss(str);
    std::string byte_str;

    while (iss >> byte_str) {
        unsigned int byte_val;
        std::istringstream(byte_str) >> std::hex >> byte_val;
        data.push_back(static_cast<uint8_t>(byte_val));
    }

    return data;
}

/// Send mode: send a single CAN frame and exit
int mode_send(int node_id, uint32_t can_id, const Vector<uint8_t> &data) {
    echo::info("CAN Node ", node_id, " - SEND mode").cyan().bold();

    // Create ShmLink
    String node_name("can_node_");
    char buf[16];
    snprintf(buf, sizeof(buf), "%d", node_id);
    node_name = node_name + String(buf);

    auto link_result = ShmLink::attach(node_name);
    if (!link_result.is_ok()) {
        echo::error("Failed to attach to ShmLink: ", link_result.error().message.c_str()).red();
        return 1;
    }
    auto link = std::make_shared<ShmLink>(std::move(link_result.value()));

    // Create CAN endpoint
    CanConfig config;
    config.bitrate = 500000;
    CanEndpoint can_ep(link, config, node_id);

    // Create CAN frame
    can_frame frame;
    frame.can_id = can_id;
    frame.can_dlc = std::min(data.size(), size_t(8));
    std::memset(frame.data, 0, 8);
    std::memcpy(frame.data, data.data(), frame.can_dlc);

    echo::info("Sending CAN frame:").green();
    echo::info("  ID: 0x", std::hex, std::setfill('0'), std::setw(3), can_id, std::dec);
    echo::info("  DLC: ", (int)frame.can_dlc);

    if (frame.can_dlc > 0) {
        std::stringstream ss;
        ss << "  Data: ";
        for (int i = 0; i < frame.can_dlc; ++i) {
            ss << std::hex << std::setfill('0') << std::setw(2) << (int)frame.data[i];
            if (i < frame.can_dlc - 1)
                ss << " ";
        }
        echo::info(ss.str().c_str());
    }

    auto result = can_ep.send_can(frame);
    if (!result.is_ok()) {
        echo::error("Send failed: ", result.error().message.c_str()).red();
        return 1;
    }

    echo::info("Frame sent successfully!").green().bold();
    return 0;
}

/// Receive mode: receive and print CAN frames
int mode_recv(int node_id, int timeout_ms = 5000) {
    echo::info("CAN Node ", node_id, " - RECV mode").cyan().bold();
    echo::info("Waiting for CAN frames (timeout: ", timeout_ms, " ms)...").cyan();

    // Create ShmLink
    String node_name("can_node_");
    char buf[16];
    snprintf(buf, sizeof(buf), "%d", node_id);
    node_name = node_name + String(buf);

    auto link_result = ShmLink::attach(node_name);
    if (!link_result.is_ok()) {
        echo::error("Failed to attach to ShmLink: ", link_result.error().message.c_str()).red();
        return 1;
    }
    auto link = std::make_shared<ShmLink>(std::move(link_result.value()));

    // Create CAN endpoint
    CanConfig config;
    config.bitrate = 500000;
    CanEndpoint can_ep(link, config, node_id);

    // Receive frames
    uint64_t start_time = now_ns();
    int frames_received = 0;

    while (true) {
        // Check timeout
        uint64_t elapsed_ms = (now_ns() - start_time) / 1000000;
        if (elapsed_ms > static_cast<uint64_t>(timeout_ms)) {
            echo::warn("Timeout reached").yellow();
            break;
        }

        // Try to receive
        can_ep.process();
        can_frame frame;
        auto result = can_ep.recv_can(frame);

        if (result.is_ok()) {
            frames_received++;

            echo::info("Received CAN frame #", frames_received, ":").green();
            echo::info("  ID: 0x", std::hex, std::setfill('0'), std::setw(3), (frame.can_id & CAN_EFF_MASK), std::dec);
            echo::info("  DLC: ", (int)frame.can_dlc);

            if (frame.can_dlc > 0) {
                std::stringstream ss;
                ss << "  Data: ";
                for (int i = 0; i < frame.can_dlc; ++i) {
                    ss << std::hex << std::setfill('0') << std::setw(2) << (int)frame.data[i];
                    if (i < frame.can_dlc - 1)
                        ss << " ";
                }
                echo::info(ss.str().c_str());
            }
        } else {
            // No frame available, sleep briefly
            usleep(1000); // 1 ms
        }
    }

    echo::info("Received ", frames_received, " frames").green().bold();
    return 0;
}

/// Ping-pong mode: send a frame and wait for response
int mode_pingpong(int node_id, uint32_t can_id) {
    echo::info("CAN Node ", node_id, " - PINGPONG mode").cyan().bold();

    // Create ShmLink
    String node_name("can_node_");
    char buf[16];
    snprintf(buf, sizeof(buf), "%d", node_id);
    node_name = node_name + String(buf);

    auto link_result = ShmLink::attach(node_name);
    if (!link_result.is_ok()) {
        echo::error("Failed to attach to ShmLink: ", link_result.error().message.c_str()).red();
        return 1;
    }
    auto link = std::make_shared<ShmLink>(std::move(link_result.value()));

    // Create CAN endpoint
    CanConfig config;
    config.bitrate = 500000;
    CanEndpoint can_ep(link, config, node_id);

    // Send ping
    can_frame ping;
    ping.can_id = can_id;
    ping.can_dlc = 4;
    ping.data[0] = 'P';
    ping.data[1] = 'I';
    ping.data[2] = 'N';
    ping.data[3] = 'G';

    echo::info("Sending PING (ID=0x", std::hex, can_id, std::dec, ")...").green();

    uint64_t start_time = now_ns();
    auto send_result = can_ep.send_can(ping);
    if (!send_result.is_ok()) {
        echo::error("Send failed: ", send_result.error().message.c_str()).red();
        return 1;
    }

    // Wait for pong
    echo::info("Waiting for PONG...").cyan();

    uint64_t timeout_ms = 5000;
    while (true) {
        uint64_t elapsed_ms = (now_ns() - start_time) / 1000000;
        if (elapsed_ms > timeout_ms) {
            echo::error("Timeout waiting for PONG").red();
            return 1;
        }

        can_ep.process();
        can_frame pong;
        auto recv_result = can_ep.recv_can(pong);

        if (recv_result.is_ok()) {
            uint64_t latency_us = (now_ns() - start_time) / 1000;

            echo::info("Received PONG!").green().bold();
            echo::info("  ID: 0x", std::hex, (pong.can_id & CAN_EFF_MASK), std::dec);
            echo::info("  Latency: ", latency_us, " µs");

            return 0;
        }

        usleep(100); // 100 µs
    }

    return 0;
}

int main(int argc, char **argv) {
    if (argc < 3) {
        std::cerr << "Usage: " << argv[0] << " <node_id> <mode> [can_id] [data]" << std::endl;
        std::cerr << std::endl;
        std::cerr << "Modes:" << std::endl;
        std::cerr << "  send <can_id> <data>  - Send a CAN frame" << std::endl;
        std::cerr << "  recv [timeout_ms]     - Receive CAN frames" << std::endl;
        std::cerr << "  pingpong <can_id>     - Send and wait for response" << std::endl;
        std::cerr << std::endl;
        std::cerr << "Examples:" << std::endl;
        std::cerr << "  " << argv[0] << " 0 send 0x123 \"01 02 03 04\"" << std::endl;
        std::cerr << "  " << argv[0] << " 1 recv 5000" << std::endl;
        std::cerr << "  " << argv[0] << " 0 pingpong 0x100" << std::endl;
        return 1;
    }

    int node_id = std::atoi(argv[1]);
    std::string mode = argv[2];

    if (mode == "send") {
        if (argc < 5) {
            echo::error("send mode requires <can_id> and <data>").red();
            return 1;
        }
        uint32_t can_id = std::strtoul(argv[3], nullptr, 0);
        Vector<uint8_t> data = parse_hex_data(argv[4]);
        return mode_send(node_id, can_id, data);

    } else if (mode == "recv") {
        int timeout_ms = (argc > 3) ? std::atoi(argv[3]) : 5000;
        return mode_recv(node_id, timeout_ms);

    } else if (mode == "pingpong") {
        if (argc < 4) {
            echo::error("pingpong mode requires <can_id>").red();
            return 1;
        }
        uint32_t can_id = std::strtoul(argv[3], nullptr, 0);
        return mode_pingpong(node_id, can_id);

    } else {
        echo::error("Unknown mode: ", mode.c_str()).red();
        return 1;
    }

    return 0;
}
