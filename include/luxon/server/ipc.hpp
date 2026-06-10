// Copyright (c) 2026, the Luxon Server contributors
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

#include <optional>
#include <vector>
#include <cstdint>
#include <luxon/ser_interface.hpp>

namespace server {
/// \brief Abstracts Inter-Process Communication (IPC) for games
/// Provides reliable message stream
class IPC {
public:
    ///
    /// \brief Creates new IPC channel
    /// \return IPC instance if successful
    ///
    static std::optional<IPC> create();

    ///
    /// \brief Constructs an empty, closed IPC channel
    ///
    IPC() {}

    ///
    /// \brief Connects to existing IPC channel using given file descriptor
    /// \param fd File descriptor to existing IPC socket
    ///
    explicit IPC(int fd);

    ~IPC();

    IPC(IPC&& other) noexcept;
    IPC& operator=(IPC&& other) noexcept;
    IPC(const IPC&) = delete;
    IPC& operator=(const IPC&) = delete;

    ///
    /// \brief Returns fd for use in this process
    /// \return File descriptor or -1 if not open
    ///
    int get_fd() const { return fd_; }

    ///
    /// \brief Returns child fd for use in child process
    /// \return Child file descriptor or -1 if not created via create()
    ///
    int get_child_fd() const { return child_fd_; }

    ///
    ///  \brief Closes the child fd in the parent process after fork
    ///
    void close_child_fd();

    ///
    /// \brief Serializes and sends message to other side
    /// \param msg Luxon serialization message to send
    ///
    void send_message(const luxon::ser::Message& msg);

    ///
    /// \brief Receives and deserializes exactly one message if fully available
    /// \return Deserialized message, or std::nullopt if none/incomplete/error
    ///
    std::optional<luxon::ser::Message> receive_message();

    ///
    /// \brief Checks if IPC channel is currently open
    ///
    bool is_open() const { return fd_ != -1; }

private:
    IPC(int parent_fd, int child_fd);

    int fd_{-1};
    int child_fd_{-1};

    // Buffers to handle stream fragmentation
    std::vector<uint8_t> recv_buffer_;
    std::vector<uint8_t> send_buffer_;
};
} // namespace server
