#pragma once

#include <wirebit/common/types.hpp>
#include <wirebit/endpoint.hpp>

namespace wirebit {

    /// Ethernet endpoint for packet-based communication
    /// TODO: Implement in wirebit-p3e.11
    class EthEndpoint : public Endpoint {
      public:
        Result<Unit, Error> send(const Bytes &data) override {
            (void)data;
            return Result<Unit, Error>::err(Error{100, "EthEndpoint::send not yet implemented"});
        }

        Result<Bytes, Error> recv() override {
            return Result<Bytes, Error>::err(Error{100, "EthEndpoint::recv not yet implemented"});
        }

        Result<Unit, Error> process() override {
            return Result<Unit, Error>::err(Error{100, "EthEndpoint::process not yet implemented"});
        }

        String name() const override { return "eth_endpoint"; }

        Link *link() override { return nullptr; }
    };

} // namespace wirebit
