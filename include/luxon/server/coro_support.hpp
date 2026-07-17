#pragma once

#include "global.hpp"

#ifdef LUXON_SERVER_ENABLE_COROUTINES
#include <AwaitableTask.hpp>

#define lco_return co_return
#define lco_await co_await
#define lco_background(...) ((void)(__VA_ARGS__))
#else
#define lco_return return
#define lco_await
#define lco_background(...) (__VA_ARGS__)
#endif

namespace server {
template <typename T = void>
using Awaitable =
#ifdef LUXON_SERVER_ENABLE_COROUTINES
    basiccoro::AwaitableTask<T>;
#else
    T;
#endif
} // namespace server
