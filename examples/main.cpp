#include "isobus/hardware_integration/can_hardware_interface.hpp"
#include "isobus/hardware_integration/socket_can_interface.hpp"
#include "isobus/isobus/can_network_manager.hpp"

#include <atomic>
#include <csignal>
#include <iostream>
#include <memory>
#include <thread>

static std::atomic_bool running = {true};

void signal_handler(int) { running = false; }

int main() {
    // Set up the hardware layer to use SocketCAN interface on channel "can0"
    auto can_driver = std::make_shared<isobus::SocketCANInterface>("can0");
    isobus::CANHardwareInterface::set_number_of_can_channels(1);
    isobus::CANHardwareInterface::assign_can_channel_frame_handler(0, can_driver);

    if ((!isobus::CANHardwareInterface::start()) || (!can_driver->get_is_valid())) {
        std::cout << "Failed to start hardware interface." << std::endl;
        return -1;
    }

    std::signal(SIGINT, signal_handler);

    // Create NAME
    isobus::NAME my_name(0);
    my_name.set_arbitrary_address_capable(true);
    my_name.set_industry_group(1);
    my_name.set_device_class(0);
    my_name.set_function_code(static_cast<std::uint8_t>(isobus::NAME::Function::SteeringControl));
    my_name.set_identity_number(2);
    my_name.set_ecu_instance(0);
    my_name.set_function_instance(0);
    my_name.set_device_class_instance(0);
    my_name.set_manufacturer_code(1407);

    // Create InternalControlFunction
    auto my_ecu = isobus::CANNetworkManager::CANNetwork.create_internal_control_function(my_name, 0);

    // Wait for address claiming
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));

    // Send a message
    std::array<std::uint8_t, isobus::CAN_DATA_LENGTH> message_data = {0};
    isobus::CANNetworkManager::CANNetwork.send_can_message(0xEF00, message_data.data(), isobus::CAN_DATA_LENGTH,
                                                           my_ecu);

    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    isobus::CANHardwareInterface::stop();
    return 0;
}
