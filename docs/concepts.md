# MRC Concepts

## Operator

An `Operator` consists of two components:

- `SchedulingPolicy`
- `Operation`

### Operator Concepts

```c++
template <typename T>
concept scheduling_policy = value_type_provider<T> && error_type_provider<T> && requires {
    // explicit return_type
    requires std::same_as<typename T::return_type, mrc::expected<typename T::value_type, typename T::error_type>>;

    // must be an awaitable with the expected return_type
    requires awaitable_of<T, typename T::return_type> || awaiter_of<T, typename T::return_type>;
};

template<typename T>
concept operation_typed = value_type_provider<T> && requires(T operation, typename T::data_type& data)
{
    { operation.evaluate(data); } -> awaiter_of<void>;
};

template<typename T>
concept operation_void = value_type_provider_is_void<T> && requires(T operation)
{
    { operation.evaluate(); } -> awaiter_of<void>;
};

template<typename T>
concept operation = operation_typed<T> || operation_void<T>;
```

The `SchedulingPolicy` is responsible for triggering the execution of the `Operation`. The primary execution for an operation might look like:

```c++
while(true)
{
    auto ready = co_await scheduling_policy;
    if (ready) {
      if constexpr (concepts::value_type_provider_is_void<decltype(scheduling_policy)>) {
        co_await operation.evaluate();
      } else {
        co_await operation.evaluate(ready.value());
      }
    }
}
```

## Operation

An `Operation` is the object that defines the computational portion of the streaming data workflow. An `Operation` can take a single typed input or 

### Operation Concepts

```C++
template<typename T>
concept operation_typed = value_type_provider<T> && requires(T operation, typename T::data_type& data)
{
    { operation.evaluate(data); } -> awaiter_of<void>;
};

template<typename T>
concept operation_void = value_type_provider_is_void<T> && requires(T operation)
{
    { operation.evaluate(); } -> awaiter_of<void>;
};

template<typename T>
concept operation = operation_typed<T> || operation_void<T>;

template<typename Op, typename OutputT>
concept single_output_operation = requires operation<Op> &&
```

A typed `Operation` takes a generic value in as its input and

## Channel

A Channel is multi-writer/multi-reader queue responsible for applying a back pressure policy when data is pushed/written to the `Channel`.
Channels have a fixed capacity defined at construction time. The back pressure policy is supplied by the concrete implementations and defines
the behavior of the writers and readers when the channel is full or empty.

### Channel Concepts

```c++
template<typename T>
concept writable_channel = value_type_provider<T> && requires (T channel) {
  { channel.write(std::move(data)) } -> awaitable_of<ChannelStatus>
};

template<typename T>
concept readable_channel = value_type_provider<T> && requires (T channel) {
  { channel.read() } -> awaitable_of<Expected<typename T::data_type, ChannelStatus>>;
};

template<typename T>
concept channel = writable_channel<T> && readable_channel<T> && requires (T channel) {
  { channel.close() } -> awaitable_of<void>
  { channel.is_closed() } -> std::convertible_to<bool>;
  { channel.capacity() } -> std::same_as<std::size_t>;
};
```

### Types of Channels

- BufferedChannel
  - Writes will yield if the channel is at capacity
  - Reads will yield if the channel is empty
  - Writes will preferentially resume awaiting readers
  - In the case a reader is resumed from a writer’s execution context, the writers will be rescheduled on scheduler of the caller’s coroutine or the default scheduler
- ImmediateChannel
  - Writes will suspend if there are no awaiting Readers
  - Reads will suspend if there are no awaiting Writers
  - Awaiting writers holding data are always processed first
  - Suspended writers are put in a FIFO resume linked-list after their data has been consumed
  - If no incoming (writer) data is available, writers are resumed in FIFO ordering from the resume queue
- RecentChannel<T>
  - Writes will never yield
  - If the Channel is at capacity, the oldest data at the front of the queue is popped and the newest data is pushed to the back of the queue.



## Input / Output

An `Input` is a single reader of a `IReadableChannel` used to accept incoming channel data *into* another object.
Similarly, an `Output` is a single writer of a `IWritableChannel` used to emit data from an object to channel.

## Edge






An `Edge` is the term for an `Input` and `Output` that are bound to the same `Channel`.

An `Edge` creation of a linkage between an `Output` to an `Input`, which are single writers and single readers of a `Channel` that forward the respective
`writable` and `readable` interfaces to the backing channel.

An `Output` forwards the `channel::concepts::writable` interface

An `EdgeWriter`

A `Connection`

- channel
  - write
  - read
  - close
- scheduling_term
  - ready
- operation
  - evaluate
-




NodeBase

Node - Single In / Single Out
  - Sources are <void, T>
  - "srf::node" are <T, U>
  - Sink are <T, void>


## Channel Providers

While `Channel`s provides the concrete implementation of back-pressure policies, Channels are only used to instantiate an object that providers
shared pointers to either the writable, readable or both interfaces to requesters.

### Channel Provider Concepts

```c++
struct WritableChannelProvider
{
    virtual std::shared_ptr<channel::WritableChannel> get_writable_channel();
};

struct ReadableChannelProvider
{
    virtual std::shared_ptr<channel::ReadableChannel> get_readable_channel();
};

struct ChannelProvider
{
    virtual std::shared_ptr<channel::WritableChannel> get_writable_channel();
    virtual std::shared_ptr<channel::ReadableChannel> get_readable_channel();
};
```

These object are used to provide a single instance of a writable or readable
