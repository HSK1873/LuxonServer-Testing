// Copyright (c) 2026, the Luxon Server contributors
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

#include <memory>
#include <commoncpp/pooled_thread.hpp>

namespace server {
class ServerManager;
class SideThread;

using SideThreadPtr = std::shared_ptr<SideThread>;

class SideThread : protected common::PooledThread {
protected:
    friend ServerManager;

    SideThread() { start(); }

public:
    ~SideThread() {
        shutdown();
        wait();
    }

    SideThread(const SideThread&) = delete;
    SideThread(SideThread&&) = delete;

    bool busy() const { return common::PooledThread::busy(); }

    static inline SideThreadPtr create() { return std::shared_ptr<SideThread>(new SideThread); }
};
} // namespace server
