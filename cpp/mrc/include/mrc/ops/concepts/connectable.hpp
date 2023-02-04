

#pragma once

#include "mrc/channel/v2/concepts/channel.hpp"
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
concept connectable_channel = requires { requires channel::v2::concepts::channel<T>; };

template <typename T>
concept input_connectable = requires(T t,
                                     std::shared_ptr<channel::v2::ImmediateChannel<typename T::data_type>> channel) {
                                requires schedulable<T>;
                                typename T::input_type;
                                t.connect(channel);
                            };

}  // namespace mrc::ops::concepts
