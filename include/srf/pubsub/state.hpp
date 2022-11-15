#pragma once

#include "srf/types.hpp"

namespace srf::pubsub {

enum class SubscriptionState : int
{
    Watcher   = 0,
    Connected = 1,
    Completed = 2,
    Errored   = 3,
};

// Represents a single member of a subscription. Usually have 2 members per service: publisher and subscriber
struct SubscriptionMember
{
    InstanceID instance_id;
    TagID tag;
    std::string role;
    SubscriptionState state;
};

}  // namespace srf::pubsub
