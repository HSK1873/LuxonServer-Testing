#pragma once

#include <coroutine>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace basiccoro {

struct FfiCancelledException {};

template <class T, class Initiator> class FfiAwaiter {
public:
    explicit FfiAwaiter(Initiator initiator) : initiator_(std::move(initiator)) {}

    // Tell coroutine to always suspend when hitting co_await
    bool await_ready() const noexcept { return false; }

    // Called automatically by compiler when coroutine suspends
    template <class Promise> void await_suspend(std::coroutine_handle<Promise> handle) {
        coro_ = handle;
        auto init = std::move(initiator_);
        init(static_cast<void *>(this));
    }

    // Called when coroutine resumes to unpack result
    T await_resume() {
        if (cancelled_) {
            throw FfiCancelledException();
        }

        if constexpr (!std::is_same_v<T, void>) {
            if (!result_) {
                throw std::runtime_error("FfiAwaiter: no value in result_");
            }
            return std::move(*result_);
        }
    }

    // Static utility to be called when the FFI task completes
    static void resume(void *opaque_handle, T value) {
        auto *self = static_cast<FfiAwaiter *>(opaque_handle);
        self->result_ = std::move(value);

        if (self->coro_ && !self->coro_.done()) {
            self->coro_.resume();
        }
    }

    // Static utility to destroy coroutine if FFI drops task
    static void cancel(void *opaque_handle) {
        auto *self = static_cast<FfiAwaiter *>(opaque_handle);
        self->cancelled_ = true;

        if (self->coro_ && !self->coro_.done()) {
            self->coro_.resume();
        }
    }

private:
    Initiator initiator_;
    std::coroutine_handle<> coro_ = nullptr;
    std::optional<T> result_;
    bool cancelled_ = false;
};

// Specialization for void returns
template <class Initiator> class FfiAwaiter<void, Initiator> {
public:
    explicit FfiAwaiter(Initiator initiator) : initiator_(std::move(initiator)) {}

    bool await_ready() const noexcept { return false; }

    template <class Promise> void await_suspend(std::coroutine_handle<Promise> handle) {
        coro_ = handle;
        auto init = std::move(initiator_);
        init(static_cast<void *>(this));
    }

    void await_resume() {
        if (cancelled_) {
            throw FfiCancelledException();
        }
    }

    static void resume(void *opaque_handle) {
        auto *self = static_cast<FfiAwaiter *>(opaque_handle);
        if (self->coro_ && !self->coro_.done()) {
            self->coro_.resume();
        }
    }

    static void cancel(void *opaque_handle) {
        auto *self = static_cast<FfiAwaiter *>(opaque_handle);
        self->cancelled_ = true;

        if (self->coro_ && !self->coro_.done()) {
            self->coro_.resume();
        }
    }

private:
    Initiator initiator_;
    std::coroutine_handle<> coro_ = nullptr;
    bool cancelled_ = false;
};

// Helper for template deduction, making it easy to invoke inline
template <class T, class Initiator> FfiAwaiter<T, std::decay_t<Initiator>> ffi_await(Initiator&& initiator) {
    return FfiAwaiter<T, std::decay_t<Initiator>>(std::forward<Initiator>(initiator));
}

} // namespace basiccoro
