#pragma once

#ifndef NO_HARDWARE

// Disable format-truncation warning for snprintf to ifr_name (IFNAMSIZ=16 is intentional)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation"

#include <cerrno>
#include <cstring>
#include <echo/echo.hpp>
#include <fcntl.h>
#include <linux/can.h>
#include <linux/can/raw.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>
#include <wirebit/common/time.hpp>
#include <wirebit/common/types.hpp>
#include <wirebit/frame.hpp>
#include <wirebit/link.hpp>

namespace wirebit {

    /// Configuration for SocketCAN link
    struct SocketCanConfig {
        String interface_name = "vcan0"; ///< CAN interface name (vcan0, can0, etc.)
        bool create_if_missing = true;   ///< Create interface if it doesn't exist (requires sudo)
        bool destroy_on_close = false;   ///< Destroy interface when link is closed
    };

    /// Statistics for SocketCanLink
    struct SocketCanLinkStats {
        uint64_t frames_sent = 0;
        uint64_t frames_received = 0;
        uint64_t bytes_sent = 0;
        uint64_t bytes_received = 0;
        uint64_t send_errors = 0;
        uint64_t recv_errors = 0;

        inline void reset() {
            frames_sent = 0;
            frames_received = 0;
            bytes_sent = 0;
            bytes_received = 0;
            send_errors = 0;
            recv_errors = 0;
        }
    };

    /// SocketCAN link for real/virtual CAN bus communication
    /// Bridges wirebit to Linux SocketCAN interfaces (vcan0, can0, etc.)
    ///
    /// @note Disabled when NO_HARDWARE is defined
    /// @note Interface creation/deletion requires sudo (or sudoers config)
    ///
    /// Example usage:
    /// @code
    /// auto link = SocketCanLink::create({.interface_name = "vcan0"}).value();
    /// // Now can use: candump vcan0 / cansend vcan0 123#DEADBEEF
    /// CanEndpoint can(std::make_shared<SocketCanLink>(std::move(link)), config, 1);
    /// @endcode
    class SocketCanLink : public Link {
      public:
        /// Create a new SocketCAN link
        /// @param config SocketCAN configuration
        /// @return Result containing SocketCanLink or error
        static inline Result<SocketCanLink, Error> create(const SocketCanConfig &config = {}) {
            echo::trace("Creating SocketCanLink for interface: ", config.interface_name.c_str());

            // Check if interface exists
            bool interface_exists = check_interface_exists(config.interface_name);

            if (!interface_exists) {
                if (!config.create_if_missing) {
                    echo::error("Interface ", config.interface_name.c_str(), " does not exist").red();
                    return Result<SocketCanLink, Error>::err(Error::not_found("CAN interface does not exist"));
                }

                // Create virtual CAN interface
                auto create_result = create_vcan_interface(config.interface_name);
                if (!create_result.is_ok()) {
                    return Result<SocketCanLink, Error>::err(create_result.error());
                }
            }

            // Open SocketCAN socket
            int sock_fd = socket(PF_CAN, SOCK_RAW, CAN_RAW);
            if (sock_fd < 0) {
                echo::error("Failed to open CAN socket: ", strerror(errno)).red();
                return Result<SocketCanLink, Error>::err(Error::io_error("Failed to open CAN socket"));
            }

            // Get interface index
            struct ifreq ifr;
            std::memset(&ifr, 0, sizeof(ifr));
            snprintf(ifr.ifr_name, IFNAMSIZ, "%s", config.interface_name.c_str());

            if (ioctl(sock_fd, SIOCGIFINDEX, &ifr) < 0) {
                echo::error("Failed to get interface index for ", config.interface_name.c_str(), ": ", strerror(errno))
                    .red();
                close(sock_fd);
                return Result<SocketCanLink, Error>::err(Error::io_error("Failed to get interface index"));
            }

            int if_index = ifr.ifr_ifindex;
            echo::debug("Interface ", config.interface_name.c_str(), " index: ", if_index);

            // Bind socket to interface
            struct sockaddr_can addr;
            std::memset(&addr, 0, sizeof(addr));
            addr.can_family = AF_CAN;
            addr.can_ifindex = if_index;

            if (bind(sock_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
                echo::error("Failed to bind CAN socket: ", strerror(errno)).red();
                close(sock_fd);
                return Result<SocketCanLink, Error>::err(Error::io_error("Failed to bind CAN socket"));
            }

            // Set non-blocking mode
            int flags = fcntl(sock_fd, F_GETFL, 0);
            if (flags < 0 || fcntl(sock_fd, F_SETFL, flags | O_NONBLOCK) < 0) {
                echo::error("Failed to set non-blocking mode: ", strerror(errno)).red();
                close(sock_fd);
                return Result<SocketCanLink, Error>::err(Error::io_error("Failed to set non-blocking mode"));
            }

            echo::trace("SocketCanLink created: interface=", config.interface_name.c_str(), " fd=", sock_fd).green();

            return Result<SocketCanLink, Error>::ok(SocketCanLink(sock_fd, config, !interface_exists));
        }

        /// Attach to an existing SocketCAN interface (does not create if missing)
        /// @param interface_name Name of the CAN interface (e.g., "vcan0", "can0")
        /// @return Result containing SocketCanLink or error
        static inline Result<SocketCanLink, Error> attach(const String &interface_name) {
            return create({.interface_name = interface_name, .create_if_missing = false, .destroy_on_close = false});
        }

        /// Destructor - closes socket and optionally destroys interface
        inline ~SocketCanLink() {
            if (sock_fd_ >= 0) {
                echo::debug("Closing SocketCAN fd: ", sock_fd_);
                close(sock_fd_);
                sock_fd_ = -1;
            }

            if (config_.destroy_on_close && we_created_interface_) {
                echo::trace("Destroying CAN interface: ", config_.interface_name.c_str());
                destroy_vcan_interface(config_.interface_name);
            }
        }

        /// Move constructor
        inline SocketCanLink(SocketCanLink &&other) noexcept
            : sock_fd_(other.sock_fd_), config_(other.config_), stats_(other.stats_),
              we_created_interface_(other.we_created_interface_) {
            other.sock_fd_ = -1;
            other.we_created_interface_ = false;
        }

        /// Move assignment
        inline SocketCanLink &operator=(SocketCanLink &&other) noexcept {
            if (this != &other) {
                if (sock_fd_ >= 0) {
                    close(sock_fd_);
                }
                if (config_.destroy_on_close && we_created_interface_) {
                    destroy_vcan_interface(config_.interface_name);
                }
                sock_fd_ = other.sock_fd_;
                config_ = other.config_;
                stats_ = other.stats_;
                we_created_interface_ = other.we_created_interface_;
                other.sock_fd_ = -1;
                other.we_created_interface_ = false;
            }
            return *this;
        }

        // Disable copy
        SocketCanLink(const SocketCanLink &) = delete;
        SocketCanLink &operator=(const SocketCanLink &) = delete;

        /// Send a frame through the SocketCAN interface
        /// @param frame Frame to send (payload must be a can_frame)
        /// @return Result indicating success or error
        inline Result<Unit, Error> send(const Frame &frame) override {
            if (sock_fd_ < 0) {
                return Result<Unit, Error>::err(Error::io_error("SocketCAN not open"));
            }

            // Verify frame type
            if (frame.type() != FrameType::CAN) {
                echo::warn("SocketCanLink: Non-CAN frame type, ignoring");
                return Result<Unit, Error>::err(Error::invalid_argument("Expected CAN frame type"));
            }

            // Verify payload size matches can_frame
            if (frame.payload.size() != sizeof(struct can_frame)) {
                echo::error("Invalid CAN frame payload size: ", frame.payload.size(), " (expected ",
                            sizeof(struct can_frame), ")")
                    .red();
                return Result<Unit, Error>::err(Error::invalid_argument("Invalid CAN frame payload size"));
            }

            // Extract can_frame from payload
            struct can_frame cf;
            std::memcpy(&cf, frame.payload.data(), sizeof(struct can_frame));

            // Write to socket
            ssize_t written = write(sock_fd_, &cf, sizeof(struct can_frame));
            if (written < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    echo::warn("SocketCAN write would block").yellow();
                    return Result<Unit, Error>::err(Error::timeout("SocketCAN write would block"));
                }
                echo::error("SocketCAN write failed: ", strerror(errno)).red();
                stats_.send_errors++;
                return Result<Unit, Error>::err(Error::io_error("SocketCAN write failed"));
            }

            if (written != sizeof(struct can_frame)) {
                echo::warn("SocketCAN partial write: ", written, " of ", sizeof(struct can_frame), " bytes").yellow();
                stats_.send_errors++;
                return Result<Unit, Error>::err(Error::io_error("SocketCAN partial write"));
            }

            stats_.frames_sent++;
            stats_.bytes_sent += written;

            echo::debug("SocketCanLink sent: CAN ID=0x", std::hex, (cf.can_id & 0x1FFFFFFF), std::dec,
                        " DLC=", static_cast<int>(cf.can_dlc));
            return Result<Unit, Error>::ok(Unit{});
        }

        /// Receive a frame from the SocketCAN interface (non-blocking)
        /// @return Result containing frame if available, or error
        inline Result<Frame, Error> recv() override {
            if (sock_fd_ < 0) {
                return Result<Frame, Error>::err(Error::io_error("SocketCAN not open"));
            }

            // Read can_frame from socket
            struct can_frame cf;
            ssize_t bytes_read = read(sock_fd_, &cf, sizeof(struct can_frame));

            if (bytes_read < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    return Result<Frame, Error>::err(Error::timeout("No CAN frames available"));
                }
                echo::error("SocketCAN read failed: ", strerror(errno)).red();
                stats_.recv_errors++;
                return Result<Frame, Error>::err(Error::io_error("SocketCAN read failed"));
            }

            if (bytes_read != sizeof(struct can_frame)) {
                echo::warn("SocketCAN partial read: ", bytes_read, " of ", sizeof(struct can_frame), " bytes").yellow();
                stats_.recv_errors++;
                return Result<Frame, Error>::err(Error::io_error("SocketCAN partial read"));
            }

            stats_.frames_received++;
            stats_.bytes_received += bytes_read;

            // Wrap can_frame in wirebit Frame
            Bytes payload(sizeof(struct can_frame));
            std::memcpy(payload.data(), &cf, sizeof(struct can_frame));

            Frame frame = make_frame(FrameType::CAN, std::move(payload), 0, 0);

            echo::debug("SocketCanLink recv: CAN ID=0x", std::hex, (cf.can_id & 0x1FFFFFFF), std::dec,
                        " DLC=", static_cast<int>(cf.can_dlc));

            return Result<Frame, Error>::ok(std::move(frame));
        }

        /// Check if link is ready for sending
        /// @return true if link can accept more frames
        inline bool can_send() const override { return sock_fd_ >= 0; }

        /// Check if link has frames available for receiving
        /// @return true if frames might be available
        inline bool can_recv() const override { return sock_fd_ >= 0; }

        /// Get link name/identifier
        /// @return Link name
        inline String name() const override { return String("socketcan:") + config_.interface_name; }

        /// Get the CAN interface name
        /// @return Interface name (e.g., "vcan0")
        inline const String &interface_name() const { return config_.interface_name; }

        /// Get socket file descriptor
        /// @return Socket file descriptor
        inline int socket_fd() const { return sock_fd_; }

        /// Get link statistics
        /// @return Statistics reference
        inline const SocketCanLinkStats &stats() const { return stats_; }

        /// Reset statistics
        inline void reset_stats() { stats_.reset(); }

      private:
        int sock_fd_;               ///< SocketCAN socket file descriptor
        SocketCanConfig config_;    ///< Configuration
        SocketCanLinkStats stats_;  ///< Statistics
        bool we_created_interface_; ///< True if we created the interface (for cleanup)

        /// Private constructor
        inline SocketCanLink(int sock_fd, const SocketCanConfig &config, bool we_created)
            : sock_fd_(sock_fd), config_(config), we_created_interface_(we_created) {}

        /// Check if a CAN interface exists
        /// @param iface_name Interface name to check
        /// @return true if interface exists
        static inline bool check_interface_exists(const String &iface_name) {
            int sock = socket(AF_CAN, SOCK_RAW, CAN_RAW);
            if (sock < 0) {
                return false;
            }

            struct ifreq ifr;
            std::memset(&ifr, 0, sizeof(ifr));
            snprintf(ifr.ifr_name, IFNAMSIZ, "%s", iface_name.c_str());

            bool exists = (ioctl(sock, SIOCGIFINDEX, &ifr) >= 0);
            close(sock);

            echo::debug("Interface ", iface_name.c_str(), " exists: ", exists ? "yes" : "no");
            return exists;
        }

        /// Create a virtual CAN interface using ip commands
        /// @param iface_name Interface name to create
        /// @return Result indicating success or error
        static inline Result<Unit, Error> create_vcan_interface(const String &iface_name) {
            echo::trace("Creating virtual CAN interface: ", iface_name.c_str());

            // Load vcan module (ignore if already loaded)
            int ret = system("sudo modprobe vcan 2>/dev/null");
            (void)ret; // Ignore return value - module may already be loaded

            // Create vcan interface
            char cmd[256];
            snprintf(cmd, sizeof(cmd), "sudo ip link add dev %s type vcan 2>/dev/null", iface_name.c_str());
            ret = system(cmd);
            if (ret != 0) {
                // May already exist, try to continue
                echo::warn("ip link add returned ", ret, " (interface may already exist)").yellow();
            }

            // Bring interface up
            snprintf(cmd, sizeof(cmd), "sudo ip link set %s up", iface_name.c_str());
            ret = system(cmd);
            if (ret != 0) {
                echo::error("Failed to bring up interface ", iface_name.c_str()).red();
                return Result<Unit, Error>::err(Error::io_error("Failed to bring up CAN interface"));
            }

            // Verify interface exists now
            if (!check_interface_exists(iface_name)) {
                echo::error("Interface ", iface_name.c_str(), " still does not exist after creation").red();
                return Result<Unit, Error>::err(Error::io_error("Failed to create CAN interface"));
            }

            echo::trace("Virtual CAN interface ", iface_name.c_str(), " created and up").green();
            return Result<Unit, Error>::ok(Unit{});
        }

        /// Destroy a virtual CAN interface
        /// @param iface_name Interface name to destroy
        static inline void destroy_vcan_interface(const String &iface_name) {
            echo::trace("Destroying virtual CAN interface: ", iface_name.c_str());
            char cmd[256];
            snprintf(cmd, sizeof(cmd), "sudo ip link delete %s 2>/dev/null", iface_name.c_str());
            int ret = system(cmd);
            if (ret != 0) {
                echo::warn("Failed to delete interface ", iface_name.c_str(), " (may not exist)").yellow();
            }
        }
    };

} // namespace wirebit

#pragma GCC diagnostic pop

#endif // NO_HARDWARE
