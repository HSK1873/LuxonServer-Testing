// Copyright (c) 2026, the Luxon Server contributors
// SPDX-License-Identifier: BSD-3-Clause

#include "luxon/server/game_ipc.hpp"

#include <print>
#include <cerrno>
#include <cstring>
#include <sys/socket.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <luxon/ser_ipc_binary.hpp>

namespace server {
namespace {
luxon::ser::IPCBinaryProtocol protocol;

void print_hex_dump(std::span<const uint8_t> data) {
    constexpr size_t bytes_per_line = 16;

    for (size_t i = 0; i < data.size(); i += bytes_per_line) {
        // Print memory offset
        std::print("{:08x}  ", i);

        std::string ascii_chars;
        ascii_chars.reserve(bytes_per_line);

        // Print hex bytes
        for (size_t j = 0; j < bytes_per_line; ++j) {
            // Add extra space in middle for traditional hex dump readability
            if (j == 8)
                std::print(" ");

            if (i + j < data.size()) {
                uint8_t byte = data[i + j];
                std::print("{:02x} ", byte);

                // Store ASCII representation (replace non-printable chars with dot)
                ascii_chars += std::isprint(byte) ? static_cast<char>(byte) : '.';
            } else {
                // Pad with spaces if run out of bytes on last line
                std::print("   ");
            }
        }

        // Print ASCII string at end of line
        std::println(" |{}|", ascii_chars);
    }
}
}

std::optional<GameIPC> GameIPC::create() {
    int fds[2];
    // Create local domain stream socket pair
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == -1)
        return std::nullopt;
    return GameIPC(fds[0], fds[1]);
}

GameIPC::GameIPC(int fd) : fd_(fd), child_fd_(-1) {}

GameIPC::GameIPC(int parent_fd, int child_fd) : fd_(parent_fd), child_fd_(child_fd) {}

GameIPC::~GameIPC() {
    if (fd_ != -1)
        close(fd_);
    if (child_fd_ != -1)
        close(child_fd_);
}

GameIPC::GameIPC(GameIPC&& other) noexcept : fd_(other.fd_), child_fd_(other.child_fd_), recv_buffer_(std::move(other.recv_buffer_)) {
    other.fd_ = -1;
    other.child_fd_ = -1;
}

GameIPC& GameIPC::operator=(GameIPC&& other) noexcept {
    if (this != &other) {
        if (fd_ != -1)
            close(fd_);
        if (child_fd_ != -1)
            close(child_fd_);

        fd_ = other.fd_;
        child_fd_ = other.child_fd_;
        recv_buffer_ = std::move(other.recv_buffer_);

        other.fd_ = -1;
        other.child_fd_ = -1;
    }
    return *this;
}

void GameIPC::close_child_fd() {
    if (child_fd_ != -1) {
        close(child_fd_);
        child_fd_ = -1;
    }
}

void GameIPC::send_message(const luxon::ser::Message& msg) {
    if (fd_ == -1)
        throw std::runtime_error("Attempted to send message over unconnected Game IPC!");

    auto payload_res = protocol.Serialize(msg);
    if (!payload_res.has_value())
        throw std::runtime_error("Failed to serialize Game IPC message: " + payload_res.error().message);

    const auto& payload = payload_res.value();
    std::println("Sending GameIPC payload:");
    print_hex_dump(payload);

    // Make sure size can fit in 4-byte header framing
    if (payload.size() > UINT32_MAX)
        throw std::runtime_error("Game IPC message too large!");

    uint32_t network_len = htonl(static_cast<uint32_t>(payload.size()));

    std::vector<uint8_t> frame;
    frame.reserve(sizeof(network_len) + payload.size());

    const uint8_t *len_ptr = reinterpret_cast<const uint8_t *>(&network_len);
    frame.insert(frame.end(), len_ptr, len_ptr + sizeof(network_len));
    frame.insert(frame.end(), payload.begin(), payload.end());

    // Flush to socket
    size_t written = 0;
    while (written < frame.size()) {
        ssize_t res = send(fd_, frame.data() + written, frame.size() - written, MSG_NOSIGNAL);
        if (res < 0) {
            if (errno == EINTR)
                continue;
            throw std::runtime_error("Unrecoverable communication error in Game IPC: Connection reset?");
        }
        written += res;
    }
}

std::optional<luxon::ser::Message> GameIPC::receive_message() {
    if (fd_ == -1)
        return std::nullopt;

    // Drain socket and append to buffer
    uint8_t chunk[4096];
    while (true) {
        ssize_t bytes_read = recv(fd_, chunk, sizeof(chunk), MSG_DONTWAIT);
        if (bytes_read > 0) {
            recv_buffer_.insert(recv_buffer_.end(), chunk, chunk + bytes_read);
        } else if (bytes_read == 0) {
            // Connection closed by the other end gracefully
            close(fd_);
            fd_ = -1;
            throw std::runtime_error("Unrecoverable communication error in Game IPC: Connection reset");
        } else {
            if (errno == EINTR)
                continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                break; // Buffer empty, stop reading

            // Socket Error
            close(fd_);
            fd_ = -1;
            throw std::runtime_error("Unrecoverable communication error in Game IPC: Socket error");
        }
    }

    // Check if there is enough data to parse framing length header
    if (recv_buffer_.size() < sizeof(uint32_t))
        return std::nullopt;

    uint32_t network_len;
    std::memcpy(&network_len, recv_buffer_.data(), sizeof(uint32_t));
    uint32_t payload_len = ntohl(network_len);

    // Ensure complete message has arrived
    if (recv_buffer_.size() < sizeof(uint32_t) + payload_len)
        return std::nullopt; // Partial message, wait for more data in future calls

    // Extract payload
    luxon::ser::ByteArray payload(recv_buffer_.begin() + sizeof(uint32_t), recv_buffer_.begin() + sizeof(uint32_t) + payload_len);

    // Discard processed sequence from buffer so subsequent messages drop down
    recv_buffer_.erase(recv_buffer_.begin(), recv_buffer_.begin() + sizeof(uint32_t) + payload_len);
    std::println("Received GameIPC payload:");
    print_hex_dump(payload);

    // Deserialize
    auto msg_res = protocol.Deserialize(payload);

    if (!msg_res.has_value())
        throw std::runtime_error("Failed to deserialize Game IPC message: " + msg_res.error().message);

    return std::move(msg_res.value());
}
} // namespace server
