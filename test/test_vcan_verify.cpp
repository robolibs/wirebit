#include <doctest/doctest.h>

#ifndef NO_HARDWARE

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <wirebit/wirebit.hpp>

using namespace wirebit;

TEST_CASE("SocketCanLink interface creation verified with ip command") {
    const char *iface = "wbiptest";

    // Clean up any leftover
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "sudo ip link delete %s 2>/dev/null", iface);
    [[maybe_unused]] int pre_cleanup = system(cmd);

    // Verify interface does NOT exist before
    char check_cmd[256];
    snprintf(check_cmd, sizeof(check_cmd), "ip link show %s 2>&1", iface);
    FILE *fp = popen(check_cmd, "r");
    char buf[256] = {0};
    if (fgets(buf, sizeof(buf), fp) == nullptr)
        buf[0] = '\0';
    int before_status = pclose(fp);

    std::cout << "Before creation - status: " << before_status << ", output: " << buf << std::endl;
    REQUIRE(before_status != 0); // Should fail - interface doesn't exist

    // Create SocketCanLink
    SocketCanConfig config{
        .interface_name = String(iface),
        .create_if_missing = true,
        .destroy_on_close = true,
    };

    {
        auto result = SocketCanLink::create(config);
        REQUIRE(result.is_ok());

        // NOW verify interface EXISTS with ip command
        fp = popen(check_cmd, "r");
        memset(buf, 0, sizeof(buf));
        if (fgets(buf, sizeof(buf), fp) == nullptr)
            buf[0] = '\0';
        int during_status = pclose(fp);

        std::cout << "During - status: " << during_status << ", output: " << buf << std::endl;
        CHECK(during_status == 0);                 // Should succeed - interface exists
        CHECK(strstr(buf, iface) != nullptr);      // Should contain interface name

    } // Destructor runs here, should delete interface

    // Verify interface is GONE after destruction
    fp = popen(check_cmd, "r");
    memset(buf, 0, sizeof(buf));
    if (fgets(buf, sizeof(buf), fp) == nullptr)
        buf[0] = '\0';
    int after_status = pclose(fp);

    std::cout << "After destruction - status: " << after_status << ", output: " << buf << std::endl;
    CHECK(after_status != 0); // Should fail - interface was destroyed
}

#else

TEST_CASE("SocketCanLink interface verification requires hardware support") { REQUIRE(true); }

#endif
