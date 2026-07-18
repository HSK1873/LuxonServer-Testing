#include "SingleEvent.hpp"

#include <iostream>
#include <exception>
#include <utility>
#include <vector>

namespace basiccoro {

detail::SingleEventBase::SingleEventBase(detail::SingleEventBase&& other) noexcept
    : waiting_(std::move(other.waiting_)), isSet_(std::exchange(other.isSet_, false)) {}

detail::SingleEventBase& detail::SingleEventBase::operator=(detail::SingleEventBase&& other) noexcept {
    if (this != &other) {
        waiting_ = std::move(other.waiting_);
        isSet_ = std::exchange(other.isSet_, false);
    }
    return *this;
}

detail::SingleEventBase::~SingleEventBase() = default;

void detail::SingleEventBase::set_common() {
    if (!isSet_) {
        if (waiting_.empty()) {
            isSet_ = true;
        } else {
            // resuming coroutines can result in
            // consecutive co_awaits on this object
            std::vector<std::coroutine_handle<>> temp;
            temp.swap(waiting_);

            for (auto handle : temp) {
                if (!handle)
                    continue;

                try {
                    handle.resume();
                } catch (...) {
                    // This should never happen!
                    std::cerr << "basiccoro: Coroutine resume in event setter has thrown an exception that escaped the promise boundary.\n"
                              << "basiccoro: Terminating." << std::endl;
                    std::terminate();
                }
            }
        }
    }
}

SingleEvent<void>::awaiter SingleEvent<void>::operator co_await() noexcept { return awaiter{*this}; }

} // namespace basiccoro
