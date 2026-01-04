#pragma once

#include <wirebit/common/types.hpp>
#include <wirebit/endpoint.hpp>

namespace wirebit {

    /// Serial endpoint for byte-stream communication
    /// TODO: Implement in wirebit-p3e.9
    class SerialEndpoint : public Endpoint {
      public:
        Result<Unit, Error> send(const Bytes &data) override {
            (void)data;
            return Result<Unit, Error>::err(Error{100, "SerialEndpoint::send not yet implemented"});
        }

        Result<Bytes, Error> recv() override {
            return Result<Bytes, Error>::err(Error{100, "SerialEndpoint::recv not yet implemented"});
        }

        Result<Unit, Error> process() override {
            return Result<Unit, Error>::err(Error{100, "SerialEndpoint::process not yet implemented"});
        }

        String name() const override { return "serial_endpoint"; }

        Link *link() override { return nullptr; }
    };

} // namespace wirebit
