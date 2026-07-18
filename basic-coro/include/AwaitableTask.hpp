#pragma once

#include <atomic>
#include <concepts>
#include <coroutine>
#include <exception>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace basiccoro {
namespace detail {

template <class Derived> struct PromiseBase {
    std::exception_ptr exception_;

    auto get_return_object() noexcept { return std::coroutine_handle<Derived>::from_promise(static_cast<Derived&>(*this)); }
    void unhandled_exception() noexcept { exception_ = std::current_exception(); }
};

template <class Derived, class T> struct ValuePromise : public PromiseBase<Derived> {
    static_assert(std::movable<T>, "T must be movable");

    using value_type = T;
    std::optional<T> val_;

    void return_value(T&& t) { val_.emplace(std::move(t)); }

    void return_value(const T& t)
        requires std::copy_constructible<T>
    {
        val_.emplace(t);
    }

    template <class U>
        requires(!std::same_as<std::remove_cvref_t<U>, T> && std::constructible_from<T, U &&>)
    void return_value(U&& u) {
        val_.emplace(std::forward<U>(u));
    }
};

template <class Derived> struct ValuePromise<Derived, void> : public PromiseBase<Derived> {
    using value_type = void;
    void return_void() noexcept {}
};

template <class T> class AwaitablePromise : public ValuePromise<AwaitablePromise<T>, T> {
public:
    auto initial_suspend() noexcept { return std::suspend_never(); }

    auto final_suspend() noexcept {
        struct FinalAwaiter {
            bool await_ready() const noexcept { return false; }

            std::coroutine_handle<> await_suspend(std::coroutine_handle<AwaitablePromise> handle) noexcept {
                auto& promise = handle.promise();
                void *previous = promise.waiting_.exchange(completedState(), std::memory_order_acq_rel);

                if (!previous || previous == completedState()) {
                    return std::noop_coroutine();
                }

                return std::coroutine_handle<>::from_address(previous);
            }

            void await_resume() const noexcept {}
        };

        return FinalAwaiter{};
    }

    bool completed() const noexcept { return waiting_.load(std::memory_order_acquire) == completedState(); }

    bool storeWaiting(std::coroutine_handle<> handle) {
        if (!handle) {
            throw std::runtime_error("AwaitablePromise::storeWaiting(): null handle");
        }

        void *expected = nullptr;
        void *waiting = handle.address();

        if (waiting_.compare_exchange_strong(expected, waiting, std::memory_order_release, std::memory_order_acquire)) {
            return true;
        }

        if (expected == completedState()) {
            return false;
        }

        throw std::runtime_error("AwaitablePromise::storeWaiting(): already waiting");
    }

private:
    static void *completedState() noexcept { return reinterpret_cast<void *>(1); }

    std::atomic<void *> waiting_ = nullptr;
};

template <class Promise> class TaskBase {
public:
    using promise_type = Promise;

    TaskBase() noexcept = default;

    TaskBase(std::coroutine_handle<promise_type> handle) noexcept : handle_(handle) {}

    TaskBase(const TaskBase&) = delete;

    TaskBase(TaskBase&& other) noexcept : handle_(std::exchange(other.handle_, nullptr)) {}

    TaskBase& operator=(const TaskBase&) = delete;

    TaskBase& operator=(TaskBase&& other) noexcept {
        if (this != &other) {
            if (handle_) {
                handle_.destroy();
            }
            handle_ = std::exchange(other.handle_, nullptr);
        }
        return *this;
    }

    ~TaskBase() {
        if (handle_) {
            handle_.destroy();
        }
    }

    bool done() const noexcept { return !handle_ || handle_.done(); }

protected:
    std::coroutine_handle<promise_type> release() noexcept { return std::exchange(handle_, nullptr); }

    std::coroutine_handle<promise_type> handle_ = nullptr;
};

} // namespace detail

template <class T> class [[nodiscard]] AwaitableTask : public detail::TaskBase<detail::AwaitablePromise<T>> {
    using Base = detail::TaskBase<detail::AwaitablePromise<T>>;

public:
    using promise_type = typename Base::promise_type;
    using Base::Base;

    class awaiter {
    public:
        explicit awaiter(std::coroutine_handle<promise_type> handle) noexcept : handle_(handle) {}

        awaiter(const awaiter&) = delete;
        awaiter& operator=(const awaiter&) = delete;
        awaiter& operator=(awaiter&&) = delete;

        awaiter(awaiter&& other) noexcept : handle_(std::exchange(other.handle_, nullptr)) {}

        ~awaiter() {
            if (handle_) {
                handle_.destroy();
            }
        }

        bool await_ready() const noexcept { return !handle_ || handle_.promise().completed(); }

        template <class Promise> bool await_suspend(std::coroutine_handle<Promise> handle) {
            if (!handle_) {
                throw std::runtime_error("AwaitableTask::awaiter::await_suspend(): no coroutine");
            }

            return handle_.promise().storeWaiting(handle);
        }

        T await_resume() {
            if (!handle_) {
                throw std::runtime_error("AwaitableTask::awaiter::await_resume(): no coroutine");
            }

            auto& promise = handle_.promise();

            if (promise.exception_) {
                std::rethrow_exception(promise.exception_);
            }

            if constexpr (!std::is_same_v<void, T>) {
                if (!promise.val_) {
                    throw std::runtime_error("AwaitableTask::awaiter::await_resume(): no value");
                }

                T value = std::move(*promise.val_);
                promise.val_.reset();
                return value;
            }
        }

    private:
        std::coroutine_handle<promise_type> handle_ = nullptr;
    };

    awaiter operator co_await() & noexcept { return awaiter{this->release()}; }
    awaiter operator co_await() && noexcept { return awaiter{this->release()}; }

    awaiter operator co_await() const& = delete;
    awaiter operator co_await() const&& = delete;
};

} // namespace basiccoro
