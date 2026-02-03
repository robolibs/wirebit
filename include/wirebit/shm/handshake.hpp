#pragma once

#include <echo/echo.hpp>
#include <poll.h>
#include <sys/eventfd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <wirebit/common/types.hpp>

namespace wirebit {

    /// Eventfd pair for bidirectional communication
    struct EventfdPair {
        int a2b; ///< Eventfd for A→B direction
        int b2a; ///< Eventfd for B→A direction

        EventfdPair() : a2b(-1), b2a(-1) {}
        EventfdPair(int a, int b) : a2b(a), b2a(b) {}
    };

    /// Create eventfd pair and send via Unix socket
    /// @param name Link name (used for socket path)
    /// @return Result containing EventfdPair or error
    inline Result<EventfdPair, Error> create_and_send_eventfds(const String &name) {
        echo::trace("Creating eventfds for: ", name.c_str());

        String sock_path = "/tmp/wirebit_" + name + ".sock";

        // Remove existing socket
        ::unlink(sock_path.c_str());

        // Create Unix domain socket
        int sock_fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
        if (sock_fd < 0) {
            echo::error("Failed to create socket: ", strerror(errno)).red();
            return Result<EventfdPair, Error>::err(Error::io_error("socket() failed"));
        }

        // Bind socket
        struct sockaddr_un addr = {};
        addr.sun_family = AF_UNIX;
        strncpy(addr.sun_path, sock_path.c_str(), sizeof(addr.sun_path) - 1);

        if (::bind(sock_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
            echo::error("Failed to bind socket: ", strerror(errno)).red();
            ::close(sock_fd);
            return Result<EventfdPair, Error>::err(Error::io_error("bind() failed"));
        }

        // Listen
        if (::listen(sock_fd, 1) < 0) {
            echo::error("Failed to listen: ", strerror(errno)).red();
            ::close(sock_fd);
            return Result<EventfdPair, Error>::err(Error::io_error("listen() failed"));
        }

        // Create eventfds
        int efd_a2b = ::eventfd(0, EFD_SEMAPHORE | EFD_NONBLOCK);
        if (efd_a2b < 0) {
            echo::error("Failed to create eventfd A→B: ", strerror(errno)).red();
            ::close(sock_fd);
            return Result<EventfdPair, Error>::err(Error::io_error("eventfd() failed"));
        }

        int efd_b2a = ::eventfd(0, EFD_SEMAPHORE | EFD_NONBLOCK);
        if (efd_b2a < 0) {
            echo::error("Failed to create eventfd B→A: ", strerror(errno)).red();
            ::close(efd_a2b);
            ::close(sock_fd);
            return Result<EventfdPair, Error>::err(Error::io_error("eventfd() failed"));
        }

        echo::debug("Waiting for client connection...");

        // Accept connection
        int client_fd = ::accept(sock_fd, nullptr, nullptr);
        if (client_fd < 0) {
            echo::error("Failed to accept: ", strerror(errno)).red();
            ::close(efd_b2a);
            ::close(efd_a2b);
            ::close(sock_fd);
            return Result<EventfdPair, Error>::err(Error::io_error("accept() failed"));
        }

        echo::debug("Client connected, sending eventfds...");

        // Send eventfds via SCM_RIGHTS
        struct msghdr msg = {};
        char buf[1] = {'H'};
        struct iovec iov = {buf, sizeof(buf)};
        msg.msg_iov = &iov;
        msg.msg_iovlen = 1;

        char cmsg_buf[CMSG_SPACE(sizeof(int) * 2)];
        msg.msg_control = cmsg_buf;
        msg.msg_controllen = sizeof(cmsg_buf);

        struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);
        cmsg->cmsg_level = SOL_SOCKET;
        cmsg->cmsg_type = SCM_RIGHTS;
        cmsg->cmsg_len = CMSG_LEN(sizeof(int) * 2);

        int fds[2] = {efd_a2b, efd_b2a};
        memcpy(CMSG_DATA(cmsg), fds, sizeof(fds));

        if (::sendmsg(client_fd, &msg, 0) < 0) {
            echo::error("Failed to send eventfds: ", strerror(errno)).red();
            ::close(client_fd);
            ::close(efd_b2a);
            ::close(efd_a2b);
            ::close(sock_fd);
            return Result<EventfdPair, Error>::err(Error::io_error("sendmsg() failed"));
        }

        ::close(client_fd);
        ::close(sock_fd);
        ::unlink(sock_path.c_str());

        echo::trace("Eventfds created and sent: A→B=", efd_a2b, ", B→A=", efd_b2a).green();

        return Result<EventfdPair, Error>::ok(EventfdPair{efd_a2b, efd_b2a});
    }

    /// Receive eventfd pair via Unix socket
    /// @param name Link name (used for socket path)
    /// @return Result containing EventfdPair or error
    inline Result<EventfdPair, Error> receive_eventfds(const String &name) {
        echo::trace("Receiving eventfds for: ", name.c_str());

        String sock_path = "/tmp/wirebit_" + name + ".sock";

        // Create Unix domain socket
        int sock_fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
        if (sock_fd < 0) {
            echo::error("Failed to create socket: ", strerror(errno)).red();
            return Result<EventfdPair, Error>::err(Error::io_error("socket() failed"));
        }

        // Connect to server
        struct sockaddr_un addr = {};
        addr.sun_family = AF_UNIX;
        strncpy(addr.sun_path, sock_path.c_str(), sizeof(addr.sun_path) - 1);

        if (::connect(sock_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
            echo::error("Failed to connect: ", strerror(errno)).red();
            ::close(sock_fd);
            return Result<EventfdPair, Error>::err(Error::io_error("connect() failed"));
        }

        echo::debug("Connected, receiving eventfds...");

        // Receive eventfds via SCM_RIGHTS
        struct msghdr msg = {};
        char buf[1];
        struct iovec iov = {buf, sizeof(buf)};
        msg.msg_iov = &iov;
        msg.msg_iovlen = 1;

        char cmsg_buf[CMSG_SPACE(sizeof(int) * 2)];
        msg.msg_control = cmsg_buf;
        msg.msg_controllen = sizeof(cmsg_buf);

        ssize_t n = ::recvmsg(sock_fd, &msg, 0);
        if (n < 0) {
            echo::error("Failed to receive eventfds: ", strerror(errno)).red();
            ::close(sock_fd);
            return Result<EventfdPair, Error>::err(Error::io_error("recvmsg() failed"));
        }

        // Extract eventfds
        struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);
        if (!cmsg || cmsg->cmsg_type != SCM_RIGHTS) {
            echo::error("No file descriptors received").red();
            ::close(sock_fd);
            return Result<EventfdPair, Error>::err(Error::io_error("No FDs received"));
        }

        int fds[2];
        memcpy(fds, CMSG_DATA(cmsg), sizeof(fds));

        ::close(sock_fd);

        echo::trace("Eventfds received: A→B=", fds[0], ", B→A=", fds[1]).green();

        return Result<EventfdPair, Error>::ok(EventfdPair{fds[0], fds[1]});
    }

    /// Notify eventfd (write 1 to wake up waiting consumers)
    inline Result<Unit, Error> notify_eventfd(int eventfd) {
        echo::trace("Notifying eventfd: ", eventfd);

        uint64_t val = 1;
        ssize_t n = ::write(eventfd, &val, sizeof(val));
        if (n != sizeof(val)) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // Non-blocking write would block, that's okay
                return Result<Unit, Error>::ok(Unit{});
            }
            echo::error("Failed to write to eventfd: ", strerror(errno)).red();
            return Result<Unit, Error>::err(Error::io_error("eventfd write failed"));
        }

        return Result<Unit, Error>::ok(Unit{});
    }

    /// Wait on eventfd with timeout
    /// @param eventfd File descriptor to wait on
    /// @param timeout_ms Timeout in milliseconds (-1 = infinite)
    /// @return Result indicating success or timeout/error
    inline Result<Unit, Error> wait_eventfd(int eventfd, int timeout_ms = -1) {
        echo::trace("Waiting on eventfd: ", eventfd, " (timeout: ", timeout_ms, " ms)");

        struct pollfd pfd = {eventfd, POLLIN, 0};
        int ret = ::poll(&pfd, 1, timeout_ms);

        if (ret == 0) {
            echo::trace("Eventfd wait timeout");
            return Result<Unit, Error>::err(Error::timeout("poll timeout"));
        }

        if (ret < 0) {
            echo::error("poll() failed: ", strerror(errno)).red();
            return Result<Unit, Error>::err(Error::io_error("poll failed"));
        }

        // Read the eventfd to clear it
        uint64_t val;
        ssize_t n = ::read(eventfd, &val, sizeof(val));
        if (n != sizeof(val)) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // No data available, that's okay
                return Result<Unit, Error>::ok(Unit{});
            }
            echo::error("Failed to read from eventfd: ", strerror(errno)).red();
            return Result<Unit, Error>::err(Error::io_error("eventfd read failed"));
        }

        echo::trace("Eventfd signaled, value: ", val);
        return Result<Unit, Error>::ok(Unit{});
    }

} // namespace wirebit
