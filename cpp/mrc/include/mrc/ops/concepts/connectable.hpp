

#pragma once

#include "mrc/channel/v2/concepts.hpp"
#include "mrc/channel/v2/immediate_channel.hpp"
#include "mrc/ops/concepts/schedulable.hpp"

#include <concepts>
#include <type_traits>

namespace mrc::ops {
template <typename T>
class AnyChannelReader;
}

namespace mrc::ops::concepts {

template <typename T>
concept connectable_channel =
    requires { requires channel::concepts::readable_channel<T> || channel::concepts::writable_channel<T>; };

template <typename T>
concept input_connectable =
    requires(T t, std::shared_ptr<channel::v2::ImmediateChannel<typename T::value_type>> channel) {
        requires schedulable<T>;
        typename T::input_type;
        t.connect(channel);
    };

}  // namespace mrc::ops::concepts
