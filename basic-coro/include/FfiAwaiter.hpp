#pragma once

#include <atomic>
#include <concepts>
#include <coroutine>
#include <cstddef>
#include <exception>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace basiccoro {

struct FfiCancelledException {};

namespace detail {

struct FfiStateBase {
    std::atomic<std::size_t> refs{1};
    std::mutex mutex;
    std::coroutine_handle<> coro = nullptr;
    std::exception_ptr exception_;
    bool completed = false;
    bool cancelled = false;
    bool suspended = false;
    bool ffi_armed = false;

    void addRef() noexcept { refs.fetch_add(1, std::memory_order_relaxed); }

    void releaseRef() noexcept {
        if (refs.fetch_sub(1, std::memory_order_acq_rel) == 1) {
            delete this;
        }
    }

    virtual ~FfiStateBase() = default;
};

template <class T> struct FfiState : public FfiStateBase {
    std::optional<T> result_;
};

template <> struct FfiState<void> : public FfiStateBase {};

inline void ffi_cancel_impl(FfiStateBase *state) {
    if (!state) {
        return;
    }

    std::coroutine_handle<> coro;
    bool release_ffi_ref = false;

    {
        std::scoped_lock lock(state->mutex);

        if (state->ffi_armed) {
            state->ffi_armed = false;
            release_ffi_ref = true;
            state->cancelled = true;
            state->completed = true;

            if (state->suspended && state->coro) {
                coro = state->coro;
            }
        }
    }

    if (coro) {
        coro.resume();
    }

    if (release_ffi_ref) {
        state->releaseRef();
    }
}

template <class T, class U>
    requires std::constructible_from<T, U&&>
void ffi_resume_impl(FfiState<T> *state, U&& value) {
    if (!state) {
        return;
    }

    std::coroutine_handle<> coro;
    bool release_ffi_ref = false;

    {
        std::scoped_lock lock(state->mutex);

        if (state->ffi_armed) {
            state->ffi_armed = false;
            release_ffi_ref = true;
            state->cancelled = false;
            state->completed = true;

            try {
                state->result_.emplace(std::forward<U>(value));
            } catch (...) {
                state->exception_ = std::current_exception();
            }

            if (state->suspended && state->coro) {
                coro = state->coro;
            }
        }
    }

    if (coro) {
        coro.resume();
    }

    if (release_ffi_ref) {
        state->releaseRef();
    }
}

inline void ffi_resume_impl(FfiState<void> *state) {
    if (!state) {
        return;
    }

    std::coroutine_handle<> coro;
    bool release_ffi_ref = false;

    {
        std::scoped_lock lock(state->mutex);

        if (state->ffi_armed) {
            state->ffi_armed = false;
            release_ffi_ref = true;
            state->cancelled = false;
            state->completed = true;

            if (state->suspended && state->coro) {
                coro = state->coro;
            }
        }
    }

    if (coro) {
        coro.resume();
    }

    if (release_ffi_ref) {
        state->releaseRef();
    }
}

} // namespace detail

template <class T, class U>
    requires std::constructible_from<T, U&&>
void ffi_resume(void *opaque_handle, U&& value) {
    detail::ffi_resume_impl(static_cast<detail::FfiState<T> *>(opaque_handle), std::forward<U>(value));
}

inline void ffi_resume(void *opaque_handle) { detail::ffi_resume_impl(static_cast<detail::FfiState<void> *>(opaque_handle)); }

inline void ffi_cancel(void *opaque_handle) { detail::ffi_cancel_impl(static_cast<detail::FfiStateBase *>(opaque_handle)); }

template <class T, class Initiator>
    requires std::movable<T>
class FfiAwaiter {
public:
    explicit FfiAwaiter(Initiator initiator) : initiator_(std::move(initiator)), state_(new detail::FfiState<T>()) {}

    FfiAwaiter(const FfiAwaiter&) = delete;
    FfiAwaiter& operator=(const FfiAwaiter&) = delete;
    FfiAwaiter& operator=(FfiAwaiter&&) = delete;

    FfiAwaiter(FfiAwaiter&& other) noexcept : initiator_(std::move(other.initiator_)), state_(std::exchange(other.state_, nullptr)) {}

    ~FfiAwaiter() { reset(); }

    // Tell coroutine to always go through await_suspend when hitting co_await
    bool await_ready() const noexcept { return false; }

    // Called automatically by compiler when coroutine reaches co_await
    template <class Promise> bool await_suspend(std::coroutine_handle<Promise> handle) {
        if (!state_) {
            throw std::runtime_error("FfiAwaiter: no state");
        }

        auto *state = state_;

        {
            std::scoped_lock lock(state->mutex);
            state->coro = handle;
            state->suspended = false;
            state->ffi_armed = true;
        }

        state->addRef();

        auto init = std::move(initiator_);
        try {
            init(static_cast<void *>(state));
        } catch (...) {
            std::scoped_lock lock(state->mutex);
            state->coro = nullptr;
            state->suspended = false;
            throw;
        }

        std::scoped_lock lock(state->mutex);

        if (state->completed) {
            state->coro = nullptr;
            state->suspended = false;
            return false;
        }

        state->suspended = true;
        return true;
    }

    // Called when coroutine resumes to unpack result
    T await_resume() {
        if (!state_) {
            throw std::runtime_error("FfiAwaiter: no state");
        }

        auto *state = state_;
        std::exception_ptr exception;
        std::optional<T> result;
        bool cancelled = false;

        {
            std::scoped_lock lock(state->mutex);

            if (!state->completed) {
                throw std::runtime_error("FfiAwaiter: await_resume() before completion");
            }

            exception = state->exception_;
            cancelled = state->cancelled;
            state->coro = nullptr;
            state->suspended = false;

            if (!exception && !cancelled) {
                if (!state->result_) {
                    throw std::runtime_error("FfiAwaiter: no value in result_");
                }

                result.emplace(std::move(*state->result_));
                state->result_.reset();
            }
        }

        if (exception) {
            std::rethrow_exception(exception);
        }

        if (cancelled) {
            throw FfiCancelledException();
        }

        return std::move(*result);
    }

    // Static utility to be called when the FFI task completes
    template <class U>
        requires std::constructible_from<T, U&&>
    static void resume(void *opaque_handle, U&& value) {
        ffi_resume<T>(opaque_handle, std::forward<U>(value));
    }

    // Static utility to signal cancellation if FFI drops task
    static void cancel(void *opaque_handle) { ffi_cancel(opaque_handle); }

private:
    void reset() noexcept {
        if (!state_) {
            return;
        }

        {
            std::scoped_lock lock(state_->mutex);
            state_->coro = nullptr;
            state_->suspended = false;
        }

        state_->releaseRef();
        state_ = nullptr;
    }

    Initiator initiator_;
    detail::FfiState<T> *state_ = nullptr;
};

// Specialization for void returns
template <class Initiator> class FfiAwaiter<void, Initiator> {
public:
    explicit FfiAwaiter(Initiator initiator) : initiator_(std::move(initiator)), state_(new detail::FfiState<void>()) {}

    FfiAwaiter(const FfiAwaiter&) = delete;
    FfiAwaiter& operator=(const FfiAwaiter&) = delete;
    FfiAwaiter& operator=(FfiAwaiter&&) = delete;

    FfiAwaiter(FfiAwaiter&& other) noexcept : initiator_(std::move(other.initiator_)), state_(std::exchange(other.state_, nullptr)) {}

    ~FfiAwaiter() { reset(); }

    bool await_ready() const noexcept { return false; }

    template <class Promise> bool await_suspend(std::coroutine_handle<Promise> handle) {
        if (!state_) {
            throw std::runtime_error("FfiAwaiter: no state");
        }

        auto *state = state_;

        {
            std::scoped_lock lock(state->mutex);
            state->coro = handle;
            state->suspended = false;
            state->ffi_armed = true;
        }

        state->addRef();

        auto init = std::move(initiator_);
        try {
            init(static_cast<void *>(state));
        } catch (...) {
            std::scoped_lock lock(state->mutex);
            state->coro = nullptr;
            state->suspended = false;
            throw;
        }

        std::scoped_lock lock(state->mutex);

        if (state->completed) {
            state->coro = nullptr;
            state->suspended = false;
            return false;
        }

        state->suspended = true;
        return true;
    }

    void await_resume() {
        if (!state_) {
            throw std::runtime_error("FfiAwaiter: no state");
        }

        auto *state = state_;
        std::exception_ptr exception;
        bool cancelled = false;

        {
            std::scoped_lock lock(state->mutex);

            if (!state->completed) {
                throw std::runtime_error("FfiAwaiter: await_resume() before completion");
            }

            exception = state->exception_;
            cancelled = state->cancelled;
            state->coro = nullptr;
            state->suspended = false;
        }

        if (exception) {
            std::rethrow_exception(exception);
        }

        if (cancelled) {
            throw FfiCancelledException();
        }
    }

    static void resume(void *opaque_handle) { ffi_resume(opaque_handle); }

    static void cancel(void *opaque_handle) { ffi_cancel(opaque_handle); }

private:
    void reset() noexcept {
        if (!state_) {
            return;
        }

        {
            std::scoped_lock lock(state_->mutex);
            state_->coro = nullptr;
            state_->suspended = false;
        }

        state_->releaseRef();
        state_ = nullptr;
    }

    Initiator initiator_;
    detail::FfiState<void> *state_ = nullptr;
};

// Helper for template deduction, making it easy to invoke inline
template <class T, class Initiator> FfiAwaiter<T, std::decay_t<Initiator>> ffi_await(Initiator&& initiator) {
    return FfiAwaiter<T, std::decay_t<Initiator>>(std::forward<Initiator>(initiator));
}

} // namespace basiccoro
