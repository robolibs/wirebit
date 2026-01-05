#pragma once

#ifdef HAS_HARDWARE

#include <cstring>
#include <echo/echo.hpp>
#include <fcntl.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>
#include <wirebit/common/types.hpp>
#include <wirebit/frame.hpp>
#include <wirebit/link.hpp>

namespace wirebit {

    /// Configuration for PTY link
    struct PtyConfig {
        bool auto_destroy = true; ///< Automatically close PTY on destructor
    };

    /// Statistics for PtyLink
    struct PtyLinkStats {
        uint64_t frames_sent = 0;
        uint64_t frames_received = 0;
        uint64_t bytes_sent = 0;
        uint64_t bytes_received = 0;

        inline void reset() {
            frames_sent = 0;
            frames_received = 0;
            bytes_sent = 0;
            bytes_received = 0;
        }
    };

    /// Pseudo-terminal link for serial communication
    /// Bridges wirebit to real PTY devices (/dev/pts/X)
    /// External tools like minicom, picocom can connect to the slave PTY
    ///
    /// @note Requires HAS_HARDWARE compile flag
    /// @note NO sudo required - PTY is completely userspace
    class PtyLink : public Link {
      public:
        /// Create a new PTY link
        /// @param config PTY configuration
        /// @return Result containing PtyLink or error
        static inline Result<PtyLink, Error> create(const PtyConfig &config = {}) {
            echo::info("Creating PtyLink...");

            // Open master PTY
            int master_fd = posix_openpt(O_RDWR | O_NOCTTY | O_NONBLOCK);
            if (master_fd < 0) {
                echo::error("Failed to open PTY master: ", strerror(errno)).red();
                return Result<PtyLink, Error>::err(Error::io_error("Failed to open PTY master"));
            }

            // Grant access to slave
            if (grantpt(master_fd) < 0) {
                echo::error("Failed to grant PTY access: ", strerror(errno)).red();
                close(master_fd);
                return Result<PtyLink, Error>::err(Error::io_error("Failed to grant PTY access"));
            }

            // Unlock slave
            if (unlockpt(master_fd) < 0) {
                echo::error("Failed to unlock PTY: ", strerror(errno)).red();
                close(master_fd);
                return Result<PtyLink, Error>::err(Error::io_error("Failed to unlock PTY"));
            }

            // Get slave path
            char *slave_path_cstr = ptsname(master_fd);
            if (slave_path_cstr == nullptr) {
                echo::error("Failed to get PTY slave path: ", strerror(errno)).red();
                close(master_fd);
                return Result<PtyLink, Error>::err(Error::io_error("Failed to get PTY slave path"));
            }

            String slave_path(slave_path_cstr);
            echo::info("PtyLink created: master_fd=", master_fd, " slave=", slave_path.c_str()).green();

            return Result<PtyLink, Error>::ok(PtyLink(master_fd, slave_path, config));
        }

        /// Destructor - closes PTY if auto_destroy is enabled
        inline ~PtyLink() {
            if (config_.auto_destroy && master_fd_ >= 0) {
                echo::debug("Closing PTY master fd: ", master_fd_);
                close(master_fd_);
                master_fd_ = -1;
            }
        }

        /// Move constructor
        inline PtyLink(PtyLink &&other) noexcept
            : master_fd_(other.master_fd_), slave_path_(std::move(other.slave_path_)), config_(other.config_),
              stats_(other.stats_), rx_buffer_(std::move(other.rx_buffer_)) {
            other.master_fd_ = -1;
        }

        /// Move assignment
        inline PtyLink &operator=(PtyLink &&other) noexcept {
            if (this != &other) {
                if (config_.auto_destroy && master_fd_ >= 0) {
                    close(master_fd_);
                }
                master_fd_ = other.master_fd_;
                slave_path_ = std::move(other.slave_path_);
                config_ = other.config_;
                stats_ = other.stats_;
                rx_buffer_ = std::move(other.rx_buffer_);
                other.master_fd_ = -1;
            }
            return *this;
        }

        // Disable copy
        PtyLink(const PtyLink &) = delete;
        PtyLink &operator=(const PtyLink &) = delete;

        /// Send a frame through the PTY
        /// @param frame Frame to send
        /// @return Result indicating success or error
        inline Result<Unit, Error> send(const Frame &frame) override {
            if (master_fd_ < 0) {
                return Result<Unit, Error>::err(Error::io_error("PTY not open"));
            }

            // Encode frame to bytes
            Bytes encoded = encode_frame(frame);

            echo::trace("PtyLink::send: ", encoded.size(), " bytes");

            // Write to master PTY
            ssize_t written = write(master_fd_, encoded.data(), encoded.size());
            if (written < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    echo::warn("PTY write would block").yellow();
                    return Result<Unit, Error>::err(Error::timeout("PTY write would block"));
                }
                echo::error("PTY write failed: ", strerror(errno)).red();
                return Result<Unit, Error>::err(Error::io_error("PTY write failed"));
            }

            if (static_cast<size_t>(written) != encoded.size()) {
                echo::warn("PTY partial write: ", written, " of ", encoded.size(), " bytes").yellow();
            }

            stats_.frames_sent++;
            stats_.bytes_sent += written;

            echo::debug("PtyLink sent: ", written, " bytes");
            return Result<Unit, Error>::ok(Unit{});
        }

        /// Receive a frame from the PTY (non-blocking)
        /// @return Result containing frame if available, or error
        inline Result<Frame, Error> recv() override {
            if (master_fd_ < 0) {
                return Result<Frame, Error>::err(Error::io_error("PTY not open"));
            }

            // Read available data into buffer
            uint8_t temp_buf[4096];
            ssize_t bytes_read = read(master_fd_, temp_buf, sizeof(temp_buf));

            if (bytes_read > 0) {
                rx_buffer_.insert(rx_buffer_.end(), temp_buf, temp_buf + bytes_read);
                stats_.bytes_received += bytes_read;
                echo::trace("PtyLink::recv: read ", bytes_read, " bytes, buffer now ", rx_buffer_.size());
            } else if (bytes_read < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
                echo::error("PTY read failed: ", strerror(errno)).red();
                return Result<Frame, Error>::err(Error::io_error("PTY read failed"));
            }

            // Try to decode a frame from buffer
            if (rx_buffer_.size() >= sizeof(FrameHeader)) {
                // Peek at header to get payload size
                FrameHeader header;
                std::memcpy(&header, rx_buffer_.data(), sizeof(FrameHeader));

                // Validate magic
                if (header.magic != 0x57424954) {
                    // Invalid magic - skip one byte and try again
                    echo::warn("Invalid frame magic, skipping byte").yellow();
                    rx_buffer_.erase(rx_buffer_.begin());
                    return Result<Frame, Error>::err(Error::timeout("No valid frame"));
                }

                size_t total_size = sizeof(FrameHeader) + header.payload_len + header.meta_len;
                if (rx_buffer_.size() >= total_size) {
                    // Full frame available - decode it
                    Bytes frame_data(rx_buffer_.begin(), rx_buffer_.begin() + total_size);
                    rx_buffer_.erase(rx_buffer_.begin(), rx_buffer_.begin() + total_size);

                    auto result = decode_frame(frame_data);
                    if (result.is_ok()) {
                        stats_.frames_received++;
                        echo::debug("PtyLink received frame: ", total_size, " bytes");
                    }
                    return result;
                }
            }

            return Result<Frame, Error>::err(Error::timeout("No frames available"));
        }

        /// Check if link is ready for sending
        /// @return true if link can accept more frames
        inline bool can_send() const override { return master_fd_ >= 0; }

        /// Check if link has frames available for receiving
        /// @return true if frames might be available
        inline bool can_recv() const override {
            // Can't check non-blocking without actually reading
            // Return true if buffer has data or fd is open
            return master_fd_ >= 0;
        }

        /// Get link name/identifier
        /// @return Link name
        inline String name() const override { return String("pty:") + slave_path_; }

        /// Get the slave PTY path (e.g., "/dev/pts/3")
        /// @return Slave PTY path for external connections
        inline const String &slave_path() const { return slave_path_; }

        /// Get master file descriptor
        /// @return Master PTY file descriptor
        inline int master_fd() const { return master_fd_; }

        /// Get link statistics
        /// @return Statistics reference
        inline const PtyLinkStats &stats() const { return stats_; }

        /// Reset statistics
        inline void reset_stats() { stats_.reset(); }

        /// Get receive buffer size (pending bytes)
        /// @return Number of bytes in receive buffer
        inline size_t rx_buffer_size() const { return rx_buffer_.size(); }

        /// Clear receive buffer
        inline void clear_rx_buffer() { rx_buffer_.clear(); }

      private:
        int master_fd_;      ///< Master PTY file descriptor
        String slave_path_;  ///< Slave PTY path (e.g., "/dev/pts/3")
        PtyConfig config_;   ///< Configuration
        PtyLinkStats stats_; ///< Statistics
        Bytes rx_buffer_;    ///< Receive buffer for partial frames

        /// Private constructor
        inline PtyLink(int master_fd, const String &slave_path, const PtyConfig &config)
            : master_fd_(master_fd), slave_path_(slave_path), config_(config) {}
    };

} // namespace wirebit

#endif // HAS_HARDWARE
