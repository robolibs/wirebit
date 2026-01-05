#pragma once

#ifdef HAS_HARDWARE

// System headers first
#include <cerrno>
#include <cstring>
#include <echo/echo.hpp>
#include <fcntl.h>
#include <linux/if_tun.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <unistd.h>

// Wirebit headers after system headers
#include <wirebit/common/time.hpp>
#include <wirebit/common/types.hpp>
#include <wirebit/frame.hpp>
#include <wirebit/link.hpp>

// Local constants for TUN packet handling
namespace wirebit {
    namespace detail {
        constexpr size_t TUN_IP_HLEN = 20;       ///< Minimum IPv4 header length
        constexpr size_t TUN_MTU = 1500;         ///< Default MTU
        constexpr size_t TUN_MAX_PACKET = 65535; ///< Maximum IP packet size
    } // namespace detail
} // namespace wirebit

namespace wirebit {

    /// Configuration for TUN link
    struct TunConfig {
        String interface_name = "tun0"; ///< TUN interface name
        bool create_if_missing = true;  ///< Create interface if it doesn't exist (requires sudo)
        bool destroy_on_close = false;  ///< Destroy interface when link is closed
        bool set_up_on_create = true;   ///< Bring interface up after creation
        String ip_address = "";         ///< IP address to assign (e.g., "10.0.0.1/24"), empty = no assignment
    };

    /// Statistics for TunLink
    struct TunLinkStats {
        uint64_t packets_sent = 0;
        uint64_t packets_received = 0;
        uint64_t bytes_sent = 0;
        uint64_t bytes_received = 0;
        uint64_t send_errors = 0;
        uint64_t recv_errors = 0;

        inline void reset() {
            packets_sent = 0;
            packets_received = 0;
            bytes_sent = 0;
            bytes_received = 0;
            send_errors = 0;
            recv_errors = 0;
        }
    };

    /// TUN link for Layer 3 (IP) packet communication
    /// Bridges wirebit to Linux TUN devices for raw IP packets
    ///
    /// @note Requires HAS_HARDWARE compile flag
    /// @note Interface creation/deletion requires sudo (or sudoers config)
    /// @note Unlike TAP (L2 Ethernet), TUN operates at L3 (IP) - no MAC addresses
    ///
    /// Example usage:
    /// @code
    /// auto tun = TunLink::create({.interface_name = "tun0", .ip_address = "10.0.0.1/24"}).value();
    /// // Now visible: ip link show tun0
    /// // Can monitor: sudo tcpdump -i tun0
    /// // Ping test: ping 10.0.0.1
    /// @endcode
    class TunLink : public Link {
      public:
        /// Create a new TUN link
        /// @param config TUN configuration
        /// @return Result containing TunLink or error
        static inline Result<TunLink, Error> create(const TunConfig &config = {}) {
            echo::info("Creating TunLink for interface: ", config.interface_name.c_str());

            // Check if interface exists
            bool interface_exists = check_interface_exists(config.interface_name);

            if (!interface_exists && config.create_if_missing) {
                // Create TUN interface
                auto create_result = create_tun_interface(config.interface_name);
                if (!create_result.is_ok()) {
                    return Result<TunLink, Error>::err(create_result.error());
                }
            }

            // Open TUN/TAP device
            int tun_fd = open("/dev/net/tun", O_RDWR | O_NONBLOCK);
            if (tun_fd < 0) {
                echo::error("Failed to open /dev/net/tun: ", strerror(errno)).red();
                return Result<TunLink, Error>::err(Error::io_error("Failed to open /dev/net/tun"));
            }

            // Configure TUN interface
            struct ifreq ifr;
            std::memset(&ifr, 0, sizeof(ifr));
            ifr.ifr_flags = IFF_TUN | IFF_NO_PI; // TUN device (L3), no packet info header
            std::strncpy(ifr.ifr_name, config.interface_name.c_str(), IFNAMSIZ - 1);
            ifr.ifr_name[IFNAMSIZ - 1] = '\0';

            if (ioctl(tun_fd, TUNSETIFF, &ifr) < 0) {
                echo::error("Failed to configure TUN interface: ", strerror(errno)).red();
                close(tun_fd);
                return Result<TunLink, Error>::err(Error::io_error("Failed to configure TUN interface"));
            }

            // Assign IP address if specified
            if (!config.ip_address.empty() && !interface_exists) {
                auto ip_result = assign_ip_address(config.interface_name, config.ip_address);
                if (!ip_result.is_ok()) {
                    echo::warn("Failed to assign IP address: ", ip_result.error().message.c_str()).yellow();
                    // Continue anyway - user can assign manually
                }
            }

            // Bring interface up if requested
            if (config.set_up_on_create && !interface_exists) {
                auto up_result = bring_interface_up(config.interface_name);
                if (!up_result.is_ok()) {
                    echo::warn("Failed to bring interface up: ", up_result.error().message.c_str()).yellow();
                    // Continue anyway - user can bring it up manually
                }
            }

            echo::info("TunLink created: interface=", config.interface_name.c_str(), " fd=", tun_fd).green();

            return Result<TunLink, Error>::ok(TunLink(tun_fd, config, !interface_exists && config.create_if_missing));
        }

        /// Attach to an existing TUN interface (does not create if missing)
        /// @param interface_name Name of the TUN interface (e.g., "tun0")
        /// @return Result containing TunLink or error
        static inline Result<TunLink, Error> attach(const String &interface_name) {
            return create({.interface_name = interface_name,
                           .create_if_missing = false,
                           .destroy_on_close = false,
                           .set_up_on_create = false,
                           .ip_address = ""});
        }

        /// Destructor - closes fd and optionally destroys interface
        inline ~TunLink() {
            if (tun_fd_ >= 0) {
                echo::debug("Closing TUN fd: ", tun_fd_);
                close(tun_fd_);
                tun_fd_ = -1;
            }

            if (config_.destroy_on_close && we_created_interface_) {
                echo::info("Destroying TUN interface: ", config_.interface_name.c_str());
                destroy_tun_interface(config_.interface_name);
            }
        }

        /// Move constructor
        inline TunLink(TunLink &&other) noexcept
            : tun_fd_(other.tun_fd_), config_(other.config_), stats_(other.stats_),
              we_created_interface_(other.we_created_interface_) {
            other.tun_fd_ = -1;
            other.we_created_interface_ = false;
        }

        /// Move assignment
        inline TunLink &operator=(TunLink &&other) noexcept {
            if (this != &other) {
                if (tun_fd_ >= 0) {
                    close(tun_fd_);
                }
                if (config_.destroy_on_close && we_created_interface_) {
                    destroy_tun_interface(config_.interface_name);
                }
                tun_fd_ = other.tun_fd_;
                config_ = other.config_;
                stats_ = other.stats_;
                we_created_interface_ = other.we_created_interface_;
                other.tun_fd_ = -1;
                other.we_created_interface_ = false;
            }
            return *this;
        }

        // Disable copy
        TunLink(const TunLink &) = delete;
        TunLink &operator=(const TunLink &) = delete;

        /// Send a frame through the TUN interface
        /// @param frame Frame to send (payload must be a raw IP packet)
        /// @return Result indicating success or error
        inline Result<Unit, Error> send(const Frame &frame) override {
            if (tun_fd_ < 0) {
                return Result<Unit, Error>::err(Error::io_error("TUN not open"));
            }

            // Verify frame type
            if (frame.type() != FrameType::IP) {
                echo::warn("TunLink: Non-IP frame type, ignoring");
                return Result<Unit, Error>::err(Error::invalid_argument("Expected IP frame type"));
            }

            // Verify minimum packet size (IP header)
            if (frame.payload.size() < detail::TUN_IP_HLEN) {
                echo::error("Invalid IP packet size: ", frame.payload.size(), " (minimum ", detail::TUN_IP_HLEN, ")")
                    .red();
                return Result<Unit, Error>::err(Error::invalid_argument("IP packet too small"));
            }

            // Write raw IP packet to TUN (IFF_NO_PI means no extra header needed)
            ssize_t written = write(tun_fd_, frame.payload.data(), frame.payload.size());
            if (written < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    echo::warn("TUN write would block").yellow();
                    return Result<Unit, Error>::err(Error::timeout("TUN write would block"));
                }
                echo::error("TUN write failed: ", strerror(errno)).red();
                stats_.send_errors++;
                return Result<Unit, Error>::err(Error::io_error("TUN write failed"));
            }

            if (static_cast<size_t>(written) != frame.payload.size()) {
                echo::warn("TUN partial write: ", written, " of ", frame.payload.size(), " bytes").yellow();
                stats_.send_errors++;
                return Result<Unit, Error>::err(Error::io_error("TUN partial write"));
            }

            stats_.packets_sent++;
            stats_.bytes_sent += written;

            echo::debug("TunLink sent: ", written, " bytes");
            return Result<Unit, Error>::ok(Unit{});
        }

        /// Receive a frame from the TUN interface (non-blocking)
        /// @return Result containing frame if available, or error
        inline Result<Frame, Error> recv() override {
            if (tun_fd_ < 0) {
                return Result<Frame, Error>::err(Error::io_error("TUN not open"));
            }

            // Read raw IP packet from TUN
            uint8_t buf[detail::TUN_MAX_PACKET];
            ssize_t bytes_read = read(tun_fd_, buf, sizeof(buf));

            if (bytes_read < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    return Result<Frame, Error>::err(Error::timeout("No packets available"));
                }
                echo::error("TUN read failed: ", strerror(errno)).red();
                stats_.recv_errors++;
                return Result<Frame, Error>::err(Error::io_error("TUN read failed"));
            }

            if (bytes_read < static_cast<ssize_t>(detail::TUN_IP_HLEN)) {
                echo::warn("TUN read too small: ", bytes_read, " bytes (minimum ", detail::TUN_IP_HLEN, ")").yellow();
                stats_.recv_errors++;
                return Result<Frame, Error>::err(Error::io_error("TUN packet too small"));
            }

            stats_.packets_received++;
            stats_.bytes_received += bytes_read;

            // Wrap raw IP packet in wirebit Frame
            Bytes payload(buf, buf + bytes_read);
            Frame frame = make_frame(FrameType::IP, std::move(payload), 0, 0);

            echo::debug("TunLink recv: ", bytes_read, " bytes");

            return Result<Frame, Error>::ok(std::move(frame));
        }

        /// Check if link is ready for sending
        /// @return true if link can accept more packets
        inline bool can_send() const override { return tun_fd_ >= 0; }

        /// Check if link has packets available for receiving
        /// @return true if packets might be available
        inline bool can_recv() const override { return tun_fd_ >= 0; }

        /// Get link name/identifier
        /// @return Link name
        inline String name() const override { return String("tun:") + config_.interface_name; }

        /// Get the TUN interface name
        /// @return Interface name (e.g., "tun0")
        inline const String &interface_name() const { return config_.interface_name; }

        /// Get TUN file descriptor
        /// @return TUN file descriptor
        inline int tun_fd() const { return tun_fd_; }

        /// Get link statistics
        /// @return Statistics reference
        inline const TunLinkStats &stats() const { return stats_; }

        /// Reset statistics
        inline void reset_stats() { stats_.reset(); }

      private:
        int tun_fd_;                ///< TUN file descriptor
        TunConfig config_;          ///< Configuration
        TunLinkStats stats_;        ///< Statistics
        bool we_created_interface_; ///< True if we created the interface (for cleanup)

        /// Private constructor
        inline TunLink(int tun_fd, const TunConfig &config, bool we_created)
            : tun_fd_(tun_fd), config_(config), we_created_interface_(we_created) {}

        /// Check if a network interface exists
        /// @param iface_name Interface name to check
        /// @return true if interface exists
        static inline bool check_interface_exists(const String &iface_name) {
            int sock = socket(AF_INET, SOCK_DGRAM, 0);
            if (sock < 0) {
                return false;
            }

            struct ifreq ifr;
            std::memset(&ifr, 0, sizeof(ifr));
            std::strncpy(ifr.ifr_name, iface_name.c_str(), IFNAMSIZ - 1);
            ifr.ifr_name[IFNAMSIZ - 1] = '\0';

            bool exists = (ioctl(sock, SIOCGIFINDEX, &ifr) >= 0);
            close(sock);

            echo::debug("Interface ", iface_name.c_str(), " exists: ", exists ? "yes" : "no");
            return exists;
        }

        /// Create a TUN interface using ip commands
        /// @param iface_name Interface name to create
        /// @return Result indicating success or error
        static inline Result<Unit, Error> create_tun_interface(const String &iface_name) {
            echo::info("Creating TUN interface: ", iface_name.c_str());

            // Get current user for ownership
            const char *user = getenv("USER");
            if (user == nullptr) {
                user = "root";
            }

            // Create TUN interface
            String cmd = String("sudo ip tuntap add dev ") + iface_name + " mode tun user " + user + " 2>/dev/null";
            int ret = system(cmd.c_str());
            if (ret != 0) {
                // May already exist, check
                if (check_interface_exists(iface_name)) {
                    echo::warn("TUN interface ", iface_name.c_str(), " already exists").yellow();
                    return Result<Unit, Error>::ok(Unit{});
                }
                echo::error("Failed to create TUN interface ", iface_name.c_str()).red();
                return Result<Unit, Error>::err(Error::io_error("Failed to create TUN interface"));
            }

            echo::info("TUN interface ", iface_name.c_str(), " created").green();
            return Result<Unit, Error>::ok(Unit{});
        }

        /// Assign IP address to interface
        /// @param iface_name Interface name
        /// @param ip_addr IP address with CIDR (e.g., "10.0.0.1/24")
        /// @return Result indicating success or error
        static inline Result<Unit, Error> assign_ip_address(const String &iface_name, const String &ip_addr) {
            echo::info("Assigning IP address ", ip_addr.c_str(), " to ", iface_name.c_str());
            String cmd = String("sudo ip addr add ") + ip_addr + " dev " + iface_name + " 2>/dev/null";
            int ret = system(cmd.c_str());
            if (ret != 0) {
                echo::error("Failed to assign IP address to ", iface_name.c_str()).red();
                return Result<Unit, Error>::err(Error::io_error("Failed to assign IP address"));
            }
            echo::info("IP address assigned").green();
            return Result<Unit, Error>::ok(Unit{});
        }

        /// Bring interface up
        /// @param iface_name Interface name
        /// @return Result indicating success or error
        static inline Result<Unit, Error> bring_interface_up(const String &iface_name) {
            String cmd = String("sudo ip link set ") + iface_name + " up 2>/dev/null";
            int ret = system(cmd.c_str());
            if (ret != 0) {
                echo::error("Failed to bring up interface ", iface_name.c_str()).red();
                return Result<Unit, Error>::err(Error::io_error("Failed to bring up interface"));
            }
            echo::info("Interface ", iface_name.c_str(), " is up").green();
            return Result<Unit, Error>::ok(Unit{});
        }

        /// Destroy a TUN interface
        /// @param iface_name Interface name to destroy
        static inline void destroy_tun_interface(const String &iface_name) {
            echo::info("Destroying TUN interface: ", iface_name.c_str());
            String cmd = String("sudo ip link delete ") + iface_name + " 2>/dev/null";
            int ret = system(cmd.c_str());
            if (ret != 0) {
                echo::warn("Failed to delete interface ", iface_name.c_str(), " (may not exist)").yellow();
            }
        }
    };

} // namespace wirebit

#endif // HAS_HARDWARE
