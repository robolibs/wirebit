#pragma once

#ifndef NO_HARDWARE

#include <cerrno>
#include <cstring>
#include <echo/echo.hpp>
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#include <wirebit/common/types.hpp>
#include <wirebit/frame.hpp>
#include <wirebit/link.hpp>

namespace wirebit {

    /// Configuration for TTY serial link
    struct TtyConfig {
        String device = "/dev/ttyUSB0"; ///< TTY device path
        uint32_t baud = 115200;         ///< Baud rate
        uint8_t data_bits = 8;          ///< Data bits (5-8)
        uint8_t stop_bits = 1;          ///< Stop bits (1 or 2)
        char parity = 'N';              ///< Parity: 'N' (none), 'E' (even), 'O' (odd)
        bool hardware_flow = false;     ///< Hardware flow control (RTS/CTS)
    };

    /// Statistics for TtyLink
    struct TtyLinkStats {
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

    /// TTY serial link for real hardware serial ports
    /// Bridges wirebit to Linux TTY devices (/dev/ttyUSB0, /dev/ttyACM0, etc.)
    ///
    /// @note Disabled when NO_HARDWARE is defined
    ///
    /// Example usage:
    /// @code
    /// auto link = TtyLink::create({.device = "/dev/ttyUSB0", .baud = 115200}).value();
    /// SerialEndpoint serial(std::make_shared<TtyLink>(std::move(link)), config, 1);
    /// @endcode
    class TtyLink : public Link {
      public:
        /// Create a new TTY link
        /// @param config TTY configuration
        /// @return Result containing TtyLink or error
        static inline Result<TtyLink, Error> create(const TtyConfig &config = {}) {
            echo::category("wirebit.tty").info("Opening TTY: ", config.device.c_str(), " @ ", config.baud, " baud");

            // Open TTY device
            int fd = open(config.device.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
            if (fd < 0) {
                echo::category("wirebit.tty").error("Failed to open ", config.device.c_str(), ": ", strerror(errno));
                return Result<TtyLink, Error>::err(Error::io_error("Failed to open TTY device"));
            }

            // Get current attributes
            struct termios tty;
            if (tcgetattr(fd, &tty) != 0) {
                echo::category("wirebit.tty").error("Failed to get TTY attributes: ", strerror(errno));
                close(fd);
                return Result<TtyLink, Error>::err(Error::io_error("Failed to get TTY attributes"));
            }

            // Set baud rate
            speed_t speed = baud_to_speed(config.baud);
            cfsetispeed(&tty, speed);
            cfsetospeed(&tty, speed);

            // Control modes
            tty.c_cflag &= ~CSIZE; // Clear size bits
            switch (config.data_bits) {
            case 5:
                tty.c_cflag |= CS5;
                break;
            case 6:
                tty.c_cflag |= CS6;
                break;
            case 7:
                tty.c_cflag |= CS7;
                break;
            case 8:
            default:
                tty.c_cflag |= CS8;
                break;
            }

            // Stop bits
            if (config.stop_bits == 2) {
                tty.c_cflag |= CSTOPB;
            } else {
                tty.c_cflag &= ~CSTOPB;
            }

            // Parity
            switch (config.parity) {
            case 'E':
            case 'e':
                tty.c_cflag |= PARENB;
                tty.c_cflag &= ~PARODD;
                break;
            case 'O':
            case 'o':
                tty.c_cflag |= PARENB;
                tty.c_cflag |= PARODD;
                break;
            case 'N':
            case 'n':
            default:
                tty.c_cflag &= ~PARENB;
                break;
            }

            // Hardware flow control
            if (config.hardware_flow) {
                tty.c_cflag |= CRTSCTS;
            } else {
                tty.c_cflag &= ~CRTSCTS;
            }

            // Enable receiver, ignore modem control lines
            tty.c_cflag |= CREAD | CLOCAL;

            // Input modes - raw input
            tty.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL);
            tty.c_iflag &= ~(IXON | IXOFF | IXANY); // No software flow control

            // Local modes - raw input (no echo, no canonical, no signals)
            tty.c_lflag &= ~(ICANON | ECHO | ECHOE | ECHONL | ISIG);

            // Output modes - raw output
            tty.c_oflag &= ~OPOST;

            // Read settings - non-blocking
            tty.c_cc[VMIN] = 0;  // Return immediately with whatever is available
            tty.c_cc[VTIME] = 0; // No timeout

            // Apply settings
            if (tcsetattr(fd, TCSANOW, &tty) != 0) {
                echo::category("wirebit.tty").error("Failed to set TTY attributes: ", strerror(errno));
                close(fd);
                return Result<TtyLink, Error>::err(Error::io_error("Failed to set TTY attributes"));
            }

            // Flush any pending data
            tcflush(fd, TCIOFLUSH);

            echo::category("wirebit.tty")
                .info("TtyLink created: ", config.device.c_str(), " fd=", fd, " ", config.baud, "/",
                      static_cast<int>(config.data_bits), config.parity, static_cast<int>(config.stop_bits));

            return Result<TtyLink, Error>::ok(TtyLink(fd, config));
        }

        /// Destructor - closes TTY
        inline ~TtyLink() {
            if (fd_ >= 0) {
                echo::category("wirebit.tty").debug("Closing TTY fd: ", fd_);
                close(fd_);
                fd_ = -1;
            }
        }

        /// Move constructor
        inline TtyLink(TtyLink &&other) noexcept
            : fd_(other.fd_), config_(std::move(other.config_)), stats_(other.stats_),
              rx_buffer_(std::move(other.rx_buffer_)) {
            other.fd_ = -1;
        }

        /// Move assignment
        inline TtyLink &operator=(TtyLink &&other) noexcept {
            if (this != &other) {
                if (fd_ >= 0) {
                    close(fd_);
                }
                fd_ = other.fd_;
                config_ = std::move(other.config_);
                stats_ = other.stats_;
                rx_buffer_ = std::move(other.rx_buffer_);
                other.fd_ = -1;
            }
            return *this;
        }

        // Disable copy
        TtyLink(const TtyLink &) = delete;
        TtyLink &operator=(const TtyLink &) = delete;

        /// Send a frame through the TTY
        /// @param frame Frame to send (payload contains raw bytes)
        /// @return Result indicating success or error
        inline Result<Unit, Error> send(const Frame &frame) override {
            if (fd_ < 0) {
                return Result<Unit, Error>::err(Error::io_error("TTY not open"));
            }

            if (frame.payload.empty()) {
                return Result<Unit, Error>::ok(Unit{});
            }

            ssize_t written = write(fd_, frame.payload.data(), frame.payload.size());
            if (written < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    return Result<Unit, Error>::err(Error::timeout("TTY write would block"));
                }
                echo::category("wirebit.tty").error("TTY write failed: ", strerror(errno));
                stats_.send_errors++;
                return Result<Unit, Error>::err(Error::io_error("TTY write failed"));
            }

            stats_.frames_sent++;
            stats_.bytes_sent += static_cast<uint64_t>(written);

            echo::category("wirebit.tty").trace("TTY sent: ", written, " bytes");
            return Result<Unit, Error>::ok(Unit{});
        }

        /// Receive a frame from the TTY (non-blocking)
        /// @return Result containing frame if data available, or error
        inline Result<Frame, Error> recv() override {
            if (fd_ < 0) {
                return Result<Frame, Error>::err(Error::io_error("TTY not open"));
            }

            // Read available data
            uint8_t buf[1024];
            ssize_t bytes_read = read(fd_, buf, sizeof(buf));

            if (bytes_read < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    return Result<Frame, Error>::err(Error::timeout("No data available"));
                }
                echo::category("wirebit.tty").error("TTY read failed: ", strerror(errno));
                stats_.recv_errors++;
                return Result<Frame, Error>::err(Error::io_error("TTY read failed"));
            }

            if (bytes_read == 0) {
                return Result<Frame, Error>::err(Error::timeout("No data available"));
            }

            stats_.frames_received++;
            stats_.bytes_received += static_cast<uint64_t>(bytes_read);

            // Wrap bytes in Frame
            Bytes payload(static_cast<size_t>(bytes_read));
            std::memcpy(payload.data(), buf, static_cast<size_t>(bytes_read));

            Frame frame = make_frame(FrameType::SERIAL, std::move(payload), 0, 0);

            echo::category("wirebit.tty").trace("TTY recv: ", bytes_read, " bytes");
            return Result<Frame, Error>::ok(std::move(frame));
        }

        /// Check if link is ready for sending
        inline bool can_send() const override { return fd_ >= 0; }

        /// Check if link has data available
        inline bool can_recv() const override { return fd_ >= 0; }

        /// Get link name
        inline String name() const override { return String("tty:") + config_.device; }

        /// Get the TTY device path
        inline const String &device() const { return config_.device; }

        /// Get file descriptor
        inline int fd() const { return fd_; }

        /// Get link statistics
        inline const TtyLinkStats &stats() const { return stats_; }

        /// Reset statistics
        inline void reset_stats() { stats_.reset(); }

        /// Get configuration
        inline const TtyConfig &config() const { return config_; }

        /// Flush input buffer
        inline void flush_input() {
            if (fd_ >= 0) {
                tcflush(fd_, TCIFLUSH);
            }
        }

        /// Flush output buffer
        inline void flush_output() {
            if (fd_ >= 0) {
                tcflush(fd_, TCOFLUSH);
            }
        }

        /// Flush both input and output buffers
        inline void flush() {
            if (fd_ >= 0) {
                tcflush(fd_, TCIOFLUSH);
            }
        }

        /// Send a break signal
        inline void send_break() {
            if (fd_ >= 0) {
                tcsendbreak(fd_, 0);
            }
        }

      private:
        int fd_;             ///< TTY file descriptor
        TtyConfig config_;   ///< Configuration
        TtyLinkStats stats_; ///< Statistics
        Bytes rx_buffer_;    ///< Receive buffer (for line buffering if needed)

        /// Private constructor
        inline TtyLink(int fd, const TtyConfig &config) : fd_(fd), config_(config) {}

        /// Convert baud rate to termios speed constant
        static inline speed_t baud_to_speed(uint32_t baud) {
            switch (baud) {
            case 50:
                return B50;
            case 75:
                return B75;
            case 110:
                return B110;
            case 134:
                return B134;
            case 150:
                return B150;
            case 200:
                return B200;
            case 300:
                return B300;
            case 600:
                return B600;
            case 1200:
                return B1200;
            case 1800:
                return B1800;
            case 2400:
                return B2400;
            case 4800:
                return B4800;
            case 9600:
                return B9600;
            case 19200:
                return B19200;
            case 38400:
                return B38400;
            case 57600:
                return B57600;
            case 115200:
                return B115200;
            case 230400:
                return B230400;
            case 460800:
                return B460800;
            case 500000:
                return B500000;
            case 576000:
                return B576000;
            case 921600:
                return B921600;
            case 1000000:
                return B1000000;
            case 1152000:
                return B1152000;
            case 1500000:
                return B1500000;
            case 2000000:
                return B2000000;
            case 2500000:
                return B2500000;
            case 3000000:
                return B3000000;
            case 3500000:
                return B3500000;
            case 4000000:
                return B4000000;
            default:
                echo::category("wirebit.tty").warn("Unknown baud rate ", baud, ", using 115200");
                return B115200;
            }
        }
    };

} // namespace wirebit

#endif // NO_HARDWARE
