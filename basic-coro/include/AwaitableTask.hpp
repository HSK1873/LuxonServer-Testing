#pragma once

#include <concepts>
#include <coroutine>
#include <exception>
#include <stdexcept>
#include <utility>

namespace basiccoro {
namespace detail {

template <class Derived> struct PromiseBase {
    std::exception_ptr exception_;

    auto get_return_object() { return std::coroutine_handle<Derived>::from_promise(static_cast<Derived&>(*this)); }
    void unhandled_exception() { exception_ = std::current_exception(); }
};

template <class Derived, class T>
    requires std::movable<T> || std::same_as<T, void>
struct ValuePromise : public PromiseBase<Derived> {
    using value_type = T;
    T val;
    void return_value(T t) { val = std::move(t); }
};

template <class Derived> struct ValuePromise<Derived, void> : public PromiseBase<Derived> {
    using value_type = void;
    void return_void() {}
};

template <class T> class AwaitablePromise : public ValuePromise<AwaitablePromise<T>, T> {
public:
    auto initial_suspend() noexcept { return std::suspend_never(); }

    auto final_suspend() noexcept {
        struct FinalAwaiter {
            std::coroutine_handle<> waiting;

            bool await_ready() const noexcept { return false; }

            std::coroutine_handle<> await_suspend(std::coroutine_handle<>) noexcept {
                if (waiting) {
                    return waiting;
                }
                return std::noop_coroutine();
            }

            void await_resume() const noexcept {}
        };

        return FinalAwaiter{waiting_};
    }

    void storeWaiting(std::coroutine_handle<> handle) {
        if (waiting_) {
            throw std::runtime_error("AwaitablePromise::storeWaiting(): already waiting");
        }
        waiting_ = handle;
    }

private:
    std::coroutine_handle<> waiting_ = nullptr;
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

    bool done() const { return !handle_ || handle_.done(); }

protected:
    std::coroutine_handle<promise_type> handle_ = nullptr;
};

} // namespace detail

template <class T> class [[nodiscard]] AwaitableTask : public detail::TaskBase<detail::AwaitablePromise<T>> {
    using Base = detail::TaskBase<detail::AwaitablePromise<T>>;

public:
    using Base::Base;

    class awaiter;
    friend class awaiter;
    awaiter operator co_await() const;
};

template <class T> struct AwaitableTask<T>::awaiter {
    bool await_ready() const noexcept { return task_.done(); }

    template <class Promise> void await_suspend(std::coroutine_handle<Promise> handle) { task_.handle_.promise().storeWaiting(handle); }

    T await_resume() {
        if (task_.handle_.promise().exception_) {
            std::rethrow_exception(task_.handle_.promise().exception_);
        }

        if constexpr (!std::is_same_v<void, T>) {
            return std::move(task_.handle_.promise().val);
        }
    }

    const AwaitableTask& task_;
};

template <class T> typename AwaitableTask<T>::awaiter AwaitableTask<T>::operator co_await() const { return awaiter{*this}; }

} // namespace basiccoro
