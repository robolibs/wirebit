#pragma once

#include <wirebit/common/types.hpp>
#include <wirebit/endpoint.hpp>

namespace wirebit {

    /// CAN endpoint for CAN bus communication
    /// TODO: Implement in wirebit-p3e.10
    class CanEndpoint : public Endpoint {
      public:
        Result<Unit, Error> send(const Bytes &data) override {
            (void)data;
            return Result<Unit, Error>::err(Error{100, "CanEndpoint::send not yet implemented"});
        }

        Result<Bytes, Error> recv() override {
            return Result<Bytes, Error>::err(Error{100, "CanEndpoint::recv not yet implemented"});
        }

        Result<Unit, Error> process() override {
            return Result<Unit, Error>::err(Error{100, "CanEndpoint::process not yet implemented"});
        }

        String name() const override { return "can_endpoint"; }

        Link *link() override { return nullptr; }
    };

} // namespace wirebit
