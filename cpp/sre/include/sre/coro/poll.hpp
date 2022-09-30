#pragma once

#include <sys/epoll.h>

namespace sre::coro {

enum class PollOp : uint64_t
{
    /// Poll for read operations.
    read = EPOLLIN,
    /// Poll for write operations.
    write = EPOLLOUT,
    /// Poll for read and write operations.
    read_write = EPOLLIN | EPOLLOUT
};

inline auto poll_op_readable(PollOp op) -> bool
{
    return (static_cast<uint64_t>(op) & EPOLLIN) != 0U;
}

inline auto poll_op_writeable(PollOp op) -> bool
{
    return (static_cast<uint64_t>(op) & EPOLLOUT) != 0U;
}

enum class PollStatus
{
    /// The poll operation was was successful.
    event,
    /// The poll operation timed out.
    timeout,
    /// The file descriptor had an error while polling.
    error,
    /// The file descriptor has been closed by the remote or an internal error/close.
    closed
};

}  // namespace sre::coro
