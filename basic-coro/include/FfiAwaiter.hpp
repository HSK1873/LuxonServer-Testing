#pragma once

#include <coroutine>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace basiccoro
{

template<class T, class Initiator>
class FfiAwaiter
{
public:
    explicit FfiAwaiter(Initiator initiator)
        : initiator_(std::move(initiator))
    {}

    // Tell the coroutine to always suspend when it hits co_await
    bool await_ready() const noexcept
    {
        return false;
    }

    // Called automatically by the compiler when the coroutine suspends
    template<class Promise>
    void await_suspend(std::coroutine_handle<Promise> handle)
    {
        coro_ = handle;
        // The awaiter lives in the suspended coroutine's heap frame.
        // Therefore, 'this' is a perfectly stable pointer to pass across the FFI.
        initiator_(static_cast<void*>(this));
    }

    // Called when the coroutine resumes to unpack the result
    T await_resume()
    {
        if constexpr (!std::is_same_v<T, void>)
        {
            if (!result_)
            {
                throw std::runtime_error("FfiAwaiter: no value in result_");
            }
            return std::move(*result_);
        }
    }

    // Static utility called by your C++ event loop when the FFI task completes
    static void resume(void* opaque_handle, T value)
    {
        auto* self = static_cast<FfiAwaiter*>(opaque_handle);
        self->result_ = std::move(value);

        if (self->coro_ && !self->coro_.done())
        {
            self->coro_.resume();
        }
    }

    // Static utility to cleanly destroy the coroutine if the FFI drops the task
    static void cancel(void* opaque_handle)
    {
        auto* self = static_cast<FfiAwaiter*>(opaque_handle);
        if (self->coro_ && !self->coro_.done())
        {
            self->coro_.destroy();
        }
    }

private:
    Initiator initiator_;
    std::coroutine_handle<> coro_ = nullptr;
    std::optional<T> result_;
};

// Specialization for void returns (matches SingleEvent<void> pattern)
template<class Initiator>
class FfiAwaiter<void, Initiator>
{
public:
    explicit FfiAwaiter(Initiator initiator)
        : initiator_(std::move(initiator))
    {}

    bool await_ready() const noexcept { return false; }

    template<class Promise>
    void await_suspend(std::coroutine_handle<Promise> handle)
    {
        coro_ = handle;
        initiator_(static_cast<void*>(this));
    }

    void await_resume() {}

    static void resume(void* opaque_handle)
    {
        auto* self = static_cast<FfiAwaiter*>(opaque_handle);
        if (self->coro_ && !self->coro_.done())
        {
            self->coro_.resume();
        }
    }

private:
    Initiator initiator_;
    std::coroutine_handle<> coro_ = nullptr;
};

// Helper for template deduction, making it easy to invoke inline
template<class T, class Initiator>
FfiAwaiter<T, std::decay_t<Initiator>> ffi_await(Initiator&& initiator)
{
    return FfiAwaiter<T, std::decay_t<Initiator>>(std::forward<Initiator>(initiator));
}

}  // namespace basiccoro
