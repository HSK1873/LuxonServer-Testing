#pragma once

#include <coroutine>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <vector>
#include <utility>

namespace basiccoro {
namespace detail {

template <class Event> class AwaiterBase {
public:
    explicit AwaiterBase(Event& event) noexcept : event_(event) {}

    bool await_ready() noexcept {
        if (event_.isSet()) {
            // unset already set event, then continue coroutine
            event_.isSet_ = false;
            return true;
        }

        return false;
    }

    void await_suspend(std::coroutine_handle<> handle) { event_.waiting_.push_back(handle); }

    typename Event::value_type await_resume() {
        if constexpr (!std::is_same_v<typename Event::value_type, void>) {
            if (!event_.result) {
                throw std::runtime_error("AwaiterBase: no value in event_.result");
            }
            return *event_.result;
        }
    }

private:
    Event& event_;
};

class SingleEventBase {
public:
    SingleEventBase() noexcept = default;

    SingleEventBase(const SingleEventBase&) = delete;
    SingleEventBase(SingleEventBase&&) noexcept;

    SingleEventBase& operator=(const SingleEventBase&) = delete;
    SingleEventBase& operator=(SingleEventBase&&) noexcept;

    ~SingleEventBase();

    [[nodiscard]]
    bool isSet() const noexcept {
        return isSet_;
    }

protected:
    void set_common();

private:
    template <class T> friend class AwaiterBase;
    std::vector<std::coroutine_handle<>> waiting_;
    bool isSet_ = false;
};

} // namespace detail

template <class T> class [[nodiscard]] SingleEvent : public detail::SingleEventBase {
public:
    using value_type = T;
    using awaiter = detail::AwaiterBase<SingleEvent<T>>;

    void set(T t) {
        result = std::move(t);
        set_common();
    }

    awaiter operator co_await() noexcept { return awaiter{*this}; }

private:
    friend awaiter;
    std::optional<T> result;
};

template <> class [[nodiscard]] SingleEvent<void> : public detail::SingleEventBase {
public:
    using value_type = void;
    using awaiter = detail::AwaiterBase<SingleEvent<void>>;

    void set() { set_common(); }

    awaiter operator co_await() noexcept;
};

} // namespace basiccoro
