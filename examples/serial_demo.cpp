/// Serial Endpoint Demo
/// Demonstrates serial communication simulation with different baud rates and configurations

#include <iostream>
#include <memory>
#include <thread>
#include <wirebit/wirebit.hpp>

using namespace wirebit;

/// Example 1: Basic serial communication
void example_basic_serial() {
    std::cout << "\n=== Example 1: Basic Serial Communication ===" << std::endl;

    // Create a shared memory link
    auto link_result = ShmLink::create(String("serial_basic"), 8192);
    if (!link_result.is_ok()) {
        std::cerr << "Failed to create link" << std::endl;
        return;
    }
    auto server_link = std::make_shared<ShmLink>(std::move(link_result.value()));

    auto client_result = ShmLink::attach(String("serial_basic"));
    if (!client_result.is_ok()) {
        std::cerr << "Failed to attach link" << std::endl;
        return;
    }
    auto client_link = std::make_shared<ShmLink>(std::move(client_result.value()));

    // Create serial endpoints with default config (115200 baud, 8N1)
    SerialConfig config;
    SerialEndpoint tx(server_link, config, 1);
    SerialEndpoint rx(client_link, config, 2);

    // Send "Hello, Serial!"
    Bytes message = {'H', 'e', 'l', 'l', 'o', ',', ' ', 'S', 'e', 'r', 'i', 'a', 'l', '!'};
    std::cout << "Sending: ";
    for (auto b : message) {
        std::cout << (char)b;
    }
    std::cout << std::endl;

    auto send_result = tx.send(message);
    if (!send_result.is_ok()) {
        std::cerr << "Send failed" << std::endl;
        return;
    }

    // Receive data
    rx.process();
    auto recv_result = rx.recv();
    if (recv_result.is_ok()) {
        auto data = recv_result.value();
        std::cout << "Received: ";
        for (auto b : data) {
            std::cout << (char)b;
        }
        std::cout << " (" << data.size() << " bytes)" << std::endl;
    }
}

/// Example 2: Different baud rates
void example_baud_rates() {
    std::cout << "\n=== Example 2: Different Baud Rates ===" << std::endl;

    struct BaudTest {
        uint32_t baud;
        const char *name;
    };

    BaudTest tests[] = {
        {9600, "9600 baud (slow)"},
        {115200, "115200 baud (standard)"},
        {921600, "921600 baud (fast)"},
    };

    for (const auto &test : tests) {
        std::cout << "\nTesting " << test.name << std::endl;

        auto link_result = ShmLink::create(String("baud_test"), 4096);
        if (!link_result.is_ok())
            continue;
        auto link = std::make_shared<ShmLink>(std::move(link_result.value()));

        SerialConfig config;
        config.baud = test.baud;
        SerialEndpoint endpoint(link, config, 1);

        // Send 10 bytes
        Bytes data(10);
        for (size_t i = 0; i < 10; ++i) {
            data[i] = static_cast<Byte>(i);
        }

        auto start = now_ns();
        endpoint.send(data);
        auto end = now_ns();

        // Calculate expected time
        uint32_t bits_per_byte = 1 + 8 + 1; // Start + data + stop
        uint64_t expected_ns = (bits_per_byte * 10 * 1000000000ULL) / test.baud;

        std::cout << "  Sent 10 bytes in " << (end - start) / 1000 << " µs" << std::endl;
        std::cout << "  Expected time: " << expected_ns / 1000 << " µs" << std::endl;
    }
}

/// Example 3: Serial configurations (parity, stop bits)
void example_serial_configs() {
    std::cout << "\n=== Example 3: Serial Configurations ===" << std::endl;

    struct ConfigTest {
        uint8_t data_bits;
        uint8_t stop_bits;
        char parity;
        const char *name;
    };

    ConfigTest tests[] = {
        {8, 1, 'N', "8N1 (standard)"},
        {7, 1, 'E', "7E1 (even parity)"},
        {8, 2, 'N', "8N2 (two stop bits)"},
        {7, 2, 'O', "7O2 (odd parity, two stop)"},
    };

    for (const auto &test : tests) {
        std::cout << "\nConfiguration: " << test.name << std::endl;

        auto link_result = ShmLink::create(String("config_test"), 4096);
        if (!link_result.is_ok())
            continue;
        auto link = std::make_shared<ShmLink>(std::move(link_result.value()));

        SerialConfig config;
        config.baud = 115200;
        config.data_bits = test.data_bits;
        config.stop_bits = test.stop_bits;
        config.parity = test.parity;

        SerialEndpoint endpoint(link, config, 1);

        // Calculate bits per byte
        uint32_t bits = 1 + test.data_bits + test.stop_bits; // Start + data + stop
        if (test.parity != 'N')
            bits++;

        std::cout << "  Bits per byte: " << bits << std::endl;
        std::cout << "  Byte time: " << (bits * 1000000000ULL / config.baud) << " ns" << std::endl;
    }
}

/// Example 4: Bidirectional communication
void example_bidirectional() {
    std::cout << "\n=== Example 4: Bidirectional Communication ===" << std::endl;

    auto server_result = ShmLink::create(String("bidir"), 8192);
    if (!server_result.is_ok())
        return;
    auto server_link = std::make_shared<ShmLink>(std::move(server_result.value()));

    auto client_result = ShmLink::attach(String("bidir"));
    if (!client_result.is_ok())
        return;
    auto client_link = std::make_shared<ShmLink>(std::move(client_result.value()));

    SerialConfig config;
    config.baud = 115200;

    SerialEndpoint device_a(server_link, config, 1);
    SerialEndpoint device_b(client_link, config, 2);

    // Device A sends to Device B
    Bytes msg_a = {'A', '-', '>', 'B'};
    std::cout << "Device A sends: ";
    for (auto b : msg_a)
        std::cout << (char)b;
    std::cout << std::endl;
    device_a.send(msg_a);

    // Device B receives
    device_b.process();
    auto recv_b = device_b.recv();
    if (recv_b.is_ok()) {
        std::cout << "Device B received: ";
        for (auto b : recv_b.value())
            std::cout << (char)b;
        std::cout << std::endl;
    }

    // Device B sends to Device A
    Bytes msg_b = {'B', '-', '>', 'A'};
    std::cout << "Device B sends: ";
    for (auto b : msg_b)
        std::cout << (char)b;
    std::cout << std::endl;
    device_b.send(msg_b);

    // Device A receives
    device_a.process();
    auto recv_a = device_a.recv();
    if (recv_a.is_ok()) {
        std::cout << "Device A received: ";
        for (auto b : recv_a.value())
            std::cout << (char)b;
        std::cout << std::endl;
    }
}

/// Example 5: Buffered reading
void example_buffered_reading() {
    std::cout << "\n=== Example 5: Buffered Reading ===" << std::endl;

    auto server_result = ShmLink::create(String("buffered"), 8192);
    if (!server_result.is_ok())
        return;
    auto server_link = std::make_shared<ShmLink>(std::move(server_result.value()));

    auto client_result = ShmLink::attach(String("buffered"));
    if (!client_result.is_ok())
        return;
    auto client_link = std::make_shared<ShmLink>(std::move(client_result.value()));

    SerialConfig tx_config;
    SerialConfig rx_config;
    rx_config.max_chunk_read = 5; // Read max 5 bytes at a time

    SerialEndpoint tx(server_link, tx_config, 1);
    SerialEndpoint rx(client_link, rx_config, 2);

    // Send 20 bytes
    Bytes data(20);
    for (size_t i = 0; i < 20; ++i) {
        data[i] = static_cast<Byte>('A' + (i % 26));
    }

    std::cout << "Sending 20 bytes..." << std::endl;
    tx.send(data);

    // Receive in chunks
    rx.process();
    int chunk_num = 1;
    while (true) {
        auto result = rx.recv();
        if (!result.is_ok())
            break;

        auto chunk = result.value();
        std::cout << "Chunk " << chunk_num++ << " (" << chunk.size() << " bytes): ";
        for (auto b : chunk) {
            std::cout << (char)b;
        }
        std::cout << std::endl;
    }
}

/// Example 6: Serial with link model (simulated errors)
void example_with_link_model() {
    std::cout << "\n=== Example 6: Serial with Link Model ===" << std::endl;

    // Create link with 10% packet loss
    LinkModel model(1000, // 1µs latency
                    0,    // no jitter
                    0.1,  // 10% drop rate
                    0.0,  // no duplication
                    0.0,  // no corruption
                    0,    // unlimited bandwidth
                    42    // seed
    );

    auto server_result = ShmLink::create(String("lossy"), 8192, &model);
    if (!server_result.is_ok())
        return;
    auto server_link = std::make_shared<ShmLink>(std::move(server_result.value()));

    auto client_result = ShmLink::attach(String("lossy"));
    if (!client_result.is_ok())
        return;
    auto client_link = std::make_shared<ShmLink>(std::move(client_result.value()));

    SerialConfig config;
    SerialEndpoint tx(server_link, config, 1);
    SerialEndpoint rx(client_link, config, 2);

    // Send 100 bytes
    Bytes data(100);
    for (size_t i = 0; i < 100; ++i) {
        data[i] = static_cast<Byte>(i);
    }

    std::cout << "Sending 100 bytes with 10% loss..." << std::endl;
    tx.send(data);

    // Receive and count
    rx.process();
    size_t received = 0;
    while (true) {
        auto result = rx.recv();
        if (!result.is_ok())
            break;
        received += result.value().size();
    }

    std::cout << "Received: " << received << " bytes (expected ~90 due to 10% loss)" << std::endl;

    auto stats = server_link->stats();
    std::cout << "Link stats: sent=" << stats.frames_sent << " dropped=" << stats.frames_dropped << std::endl;
}

int main() {
    std::cout << "=== Wirebit Serial Endpoint Demo ===" << std::endl;

    example_basic_serial();
    example_baud_rates();
    example_serial_configs();
    example_bidirectional();
    example_buffered_reading();
    example_with_link_model();

    std::cout << "\n=== Demo Complete ===" << std::endl;
    return 0;
}
