# Channel Notes

## API Changes

With the introduction of coroutines, Channels now have two distict APIs that are functionally equivalent, but result in one API being more performant but strongly typed, while the other API enables type-erasure at the cost of wrapping each concrete awaiter with a Task.

This dicotomy leads to the introduction of a two new templated methods, `async_read` and `async_write` which will preferentially return the concrete awaiter if the channel type is known or call the task-based path as a failback.

```c++

// read api

template <concepts::writable ChannelT>
[[nodiscard]] auto async_read(ChannelT& channel) -> decltype(auto)
{
    if constexpr (concepts::concrete_readable<ChannelT>)
    {
        return cpo::async_read(channel); // faster path
    }
    else
    {
        return channel.read_task(); // slower path
    }
}

// preferred way to read
auto data = channel::v2::async_read(channel);

// directly call task based read
auto data = channel.read_task();

// if the concrete `channel` type is known, it has an async_read method
auto data = channel.async_read();
```

## Distributed Schedulers

Channels by their nature as as a distributed scheduler because they hold the suspended coroutine handles and selectively choose which handles to resume.
