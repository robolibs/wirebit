#pragma once

#ifdef HAS_HARDWARE

// System headers first (they may define ETH_* macros)
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

// Local constants for TAP frame handling
// (we use raw values to avoid dependency on eth_endpoint.hpp include order)
namespace wirebit {
    namespace detail {
        constexpr size_t TAP_ETH_HLEN = 14;        ///< Ethernet header length
        constexpr size_t TAP_ETH_FRAME_LEN = 1514; ///< Maximum Ethernet frame size
    } // namespace detail
} // namespace wirebit

namespace wirebit {

    /// Configuration for TAP link
    struct TapConfig {
        String interface_name = "tap0"; ///< TAP interface name
        bool create_if_missing = true;  ///< Create interface if it doesn't exist (requires sudo)
        bool destroy_on_close = false;  ///< Destroy interface when link is closed
        bool set_up_on_create = true;   ///< Bring interface up after creation
    };

    /// Statistics for TapLink
    struct TapLinkStats {
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

    /// TAP link for real network interface communication
    /// Bridges wirebit to Linux TAP devices for L2 Ethernet frames
    ///
    /// @note Requires HAS_HARDWARE compile flag
    /// @note Interface creation/deletion requires sudo (or sudoers config)
    ///
    /// Example usage:
    /// @code
    /// auto tap = TapLink::create({.interface_name = "tap0"}).value();
    /// // Now visible: ip link show tap0
    /// // Can monitor: sudo tcpdump -i tap0
    /// EthEndpoint eth(std::make_shared<TapLink>(std::move(tap)), config, 1, mac);
    /// @endcode
    class TapLink : public Link {
      public:
        /// Create a new TAP link
        /// @param config TAP configuration
        /// @return Result containing TapLink or error
        static inline Result<TapLink, Error> create(const TapConfig &config = {}) {
            echo::info("Creating TapLink for interface: ", config.interface_name.c_str());

            // Check if interface exists
            bool interface_exists = check_interface_exists(config.interface_name);

            if (!interface_exists && config.create_if_missing) {
                // Create TAP interface
                auto create_result = create_tap_interface(config.interface_name);
                if (!create_result.is_ok()) {
                    return Result<TapLink, Error>::err(create_result.error());
                }
            }

            // Open TUN/TAP device
            int tap_fd = open("/dev/net/tun", O_RDWR | O_NONBLOCK);
            if (tap_fd < 0) {
                echo::error("Failed to open /dev/net/tun: ", strerror(errno)).red();
                return Result<TapLink, Error>::err(Error::io_error("Failed to open /dev/net/tun"));
            }

            // Configure TAP interface
            struct ifreq ifr;
            std::memset(&ifr, 0, sizeof(ifr));
            ifr.ifr_flags = IFF_TAP | IFF_NO_PI; // TAP device, no packet info header
            std::strncpy(ifr.ifr_name, config.interface_name.c_str(), IFNAMSIZ - 1);
            ifr.ifr_name[IFNAMSIZ - 1] = '\0';

            if (ioctl(tap_fd, TUNSETIFF, &ifr) < 0) {
                echo::error("Failed to configure TAP interface: ", strerror(errno)).red();
                close(tap_fd);
                return Result<TapLink, Error>::err(Error::io_error("Failed to configure TAP interface"));
            }

            // Bring interface up if requested
            if (config.set_up_on_create && !interface_exists) {
                auto up_result = bring_interface_up(config.interface_name);
                if (!up_result.is_ok()) {
                    echo::warn("Failed to bring interface up: ", up_result.error().message.c_str()).yellow();
                    // Continue anyway - user can bring it up manually
                }
            }

            echo::info("TapLink created: interface=", config.interface_name.c_str(), " fd=", tap_fd).green();

            return Result<TapLink, Error>::ok(TapLink(tap_fd, config, !interface_exists && config.create_if_missing));
        }

        /// Attach to an existing TAP interface (does not create if missing)
        /// @param interface_name Name of the TAP interface (e.g., "tap0")
        /// @return Result containing TapLink or error
        static inline Result<TapLink, Error> attach(const String &interface_name) {
            return create({.interface_name = interface_name,
                           .create_if_missing = false,
                           .destroy_on_close = false,
                           .set_up_on_create = false});
        }

        /// Destructor - closes fd and optionally destroys interface
        inline ~TapLink() {
            if (tap_fd_ >= 0) {
                echo::debug("Closing TAP fd: ", tap_fd_);
                close(tap_fd_);
                tap_fd_ = -1;
            }

            if (config_.destroy_on_close && we_created_interface_) {
                echo::info("Destroying TAP interface: ", config_.interface_name.c_str());
                destroy_tap_interface(config_.interface_name);
            }
        }

        /// Move constructor
        inline TapLink(TapLink &&other) noexcept
            : tap_fd_(other.tap_fd_), config_(other.config_), stats_(other.stats_),
              we_created_interface_(other.we_created_interface_) {
            other.tap_fd_ = -1;
            other.we_created_interface_ = false;
        }

        /// Move assignment
        inline TapLink &operator=(TapLink &&other) noexcept {
            if (this != &other) {
                if (tap_fd_ >= 0) {
                    close(tap_fd_);
                }
                if (config_.destroy_on_close && we_created_interface_) {
                    destroy_tap_interface(config_.interface_name);
                }
                tap_fd_ = other.tap_fd_;
                config_ = other.config_;
                stats_ = other.stats_;
                we_created_interface_ = other.we_created_interface_;
                other.tap_fd_ = -1;
                other.we_created_interface_ = false;
            }
            return *this;
        }

        // Disable copy
        TapLink(const TapLink &) = delete;
        TapLink &operator=(const TapLink &) = delete;

        /// Send a frame through the TAP interface
        /// @param frame Frame to send (payload must be a raw L2 Ethernet frame)
        /// @return Result indicating success or error
        inline Result<Unit, Error> send(const Frame &frame) override {
            if (tap_fd_ < 0) {
                return Result<Unit, Error>::err(Error::io_error("TAP not open"));
            }

            // Verify frame type
            if (frame.type() != FrameType::ETHERNET) {
                echo::warn("TapLink: Non-Ethernet frame type, ignoring");
                return Result<Unit, Error>::err(Error::invalid_argument("Expected Ethernet frame type"));
            }

            // Verify minimum frame size
            if (frame.payload.size() < detail::TAP_ETH_HLEN) {
                echo::error("Invalid Ethernet frame size: ", frame.payload.size(), " (minimum ", detail::TAP_ETH_HLEN,
                            ")")
                    .red();
                return Result<Unit, Error>::err(Error::invalid_argument("Ethernet frame too small"));
            }

            // Write raw L2 frame to TAP (IFF_NO_PI means no extra header needed)
            ssize_t written = write(tap_fd_, frame.payload.data(), frame.payload.size());
            if (written < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    echo::warn("TAP write would block").yellow();
                    return Result<Unit, Error>::err(Error::timeout("TAP write would block"));
                }
                echo::error("TAP write failed: ", strerror(errno)).red();
                stats_.send_errors++;
                return Result<Unit, Error>::err(Error::io_error("TAP write failed"));
            }

            if (static_cast<size_t>(written) != frame.payload.size()) {
                echo::warn("TAP partial write: ", written, " of ", frame.payload.size(), " bytes").yellow();
                stats_.send_errors++;
                return Result<Unit, Error>::err(Error::io_error("TAP partial write"));
            }

            stats_.frames_sent++;
            stats_.bytes_sent += written;

            echo::debug("TapLink sent: ", written, " bytes");
            return Result<Unit, Error>::ok(Unit{});
        }

        /// Receive a frame from the TAP interface (non-blocking)
        /// @return Result containing frame if available, or error
        inline Result<Frame, Error> recv() override {
            if (tap_fd_ < 0) {
                return Result<Frame, Error>::err(Error::io_error("TAP not open"));
            }

            // Read raw L2 frame from TAP
            // Maximum Ethernet frame size + some padding
            uint8_t buf[detail::TAP_ETH_FRAME_LEN + 64];
            ssize_t bytes_read = read(tap_fd_, buf, sizeof(buf));

            if (bytes_read < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    return Result<Frame, Error>::err(Error::timeout("No frames available"));
                }
                echo::error("TAP read failed: ", strerror(errno)).red();
                stats_.recv_errors++;
                return Result<Frame, Error>::err(Error::io_error("TAP read failed"));
            }

            if (bytes_read < static_cast<ssize_t>(detail::TAP_ETH_HLEN)) {
                echo::warn("TAP read too small: ", bytes_read, " bytes (minimum ", detail::TAP_ETH_HLEN, ")").yellow();
                stats_.recv_errors++;
                return Result<Frame, Error>::err(Error::io_error("TAP frame too small"));
            }

            stats_.frames_received++;
            stats_.bytes_received += bytes_read;

            // Wrap raw L2 frame in wirebit Frame
            Bytes payload(buf, buf + bytes_read);
            Frame frame = make_frame(FrameType::ETHERNET, std::move(payload), 0, 0);

            echo::debug("TapLink recv: ", bytes_read, " bytes");

            return Result<Frame, Error>::ok(std::move(frame));
        }

        /// Check if link is ready for sending
        /// @return true if link can accept more frames
        inline bool can_send() const override { return tap_fd_ >= 0; }

        /// Check if link has frames available for receiving
        /// @return true if frames might be available
        inline bool can_recv() const override { return tap_fd_ >= 0; }

        /// Get link name/identifier
        /// @return Link name
        inline String name() const override { return String("tap:") + config_.interface_name; }

        /// Get the TAP interface name
        /// @return Interface name (e.g., "tap0")
        inline const String &interface_name() const { return config_.interface_name; }

        /// Get TAP file descriptor
        /// @return TAP file descriptor
        inline int tap_fd() const { return tap_fd_; }

        /// Get link statistics
        /// @return Statistics reference
        inline const TapLinkStats &stats() const { return stats_; }

        /// Reset statistics
        inline void reset_stats() { stats_.reset(); }

      private:
        int tap_fd_;                ///< TAP file descriptor
        TapConfig config_;          ///< Configuration
        TapLinkStats stats_;        ///< Statistics
        bool we_created_interface_; ///< True if we created the interface (for cleanup)

        /// Private constructor
        inline TapLink(int tap_fd, const TapConfig &config, bool we_created)
            : tap_fd_(tap_fd), config_(config), we_created_interface_(we_created) {}

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

        /// Create a TAP interface using ip commands
        /// @param iface_name Interface name to create
        /// @return Result indicating success or error
        static inline Result<Unit, Error> create_tap_interface(const String &iface_name) {
            echo::info("Creating TAP interface: ", iface_name.c_str());

            // Get current user for ownership
            const char *user = getenv("USER");
            if (user == nullptr) {
                user = "root";
            }

            // Create TAP interface
            String cmd = String("sudo ip tuntap add dev ") + iface_name + " mode tap user " + user + " 2>/dev/null";
            int ret = system(cmd.c_str());
            if (ret != 0) {
                // May already exist, check
                if (check_interface_exists(iface_name)) {
                    echo::warn("TAP interface ", iface_name.c_str(), " already exists").yellow();
                    return Result<Unit, Error>::ok(Unit{});
                }
                echo::error("Failed to create TAP interface ", iface_name.c_str()).red();
                return Result<Unit, Error>::err(Error::io_error("Failed to create TAP interface"));
            }

            echo::info("TAP interface ", iface_name.c_str(), " created").green();
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

        /// Destroy a TAP interface
        /// @param iface_name Interface name to destroy
        static inline void destroy_tap_interface(const String &iface_name) {
            echo::info("Destroying TAP interface: ", iface_name.c_str());
            String cmd = String("sudo ip link delete ") + iface_name + " 2>/dev/null";
            int ret = system(cmd.c_str());
            if (ret != 0) {
                echo::warn("Failed to delete interface ", iface_name.c_str(), " (may not exist)").yellow();
            }
        }
    };

} // namespace wirebit

#endif // HAS_HARDWARE
