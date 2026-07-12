#include "lifeguard/mjpeg_server.hpp"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <utility>
#include <vector>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

namespace lifeguard {

namespace {

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

bool sendAll(int fd, const void* data, size_t size) {
    const auto* bytes = static_cast<const unsigned char*>(data);
    while (size > 0) {
        const ssize_t sent = send(fd, bytes, size, MSG_NOSIGNAL);
        if (sent <= 0) return false;
        bytes += sent;
        size -= static_cast<size_t>(sent);
    }
    return true;
}

}  // namespace

MjpegServer::MjpegServer(Options opts) : opts_(std::move(opts)) {}

MjpegServer::~MjpegServer() { stop(); }

bool MjpegServer::start() {
    if (running_.exchange(true)) return true;

    listen_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd_ < 0) {
        std::fprintf(stderr, "[stream] socket failed: %s\n", std::strerror(errno));
        running_.store(false);
        return false;
    }

    const int reuse = 1;
    setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(static_cast<uint16_t>(opts_.port));
    if (bind(listen_fd_, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0 ||
        listen(listen_fd_, 4) != 0) {
        std::fprintf(stderr, "[stream] cannot listen on port %d: %s\n",
                     opts_.port, std::strerror(errno));
        close(listen_fd_);
        listen_fd_ = -1;
        running_.store(false);
        return false;
    }

    accept_thread_ = std::thread(&MjpegServer::acceptLoop, this);
    std::fprintf(stderr, "[stream] annotated MJPEG preview on port %d\n", opts_.port);
    return true;
}

void MjpegServer::publish(const cv::Mat& bgr) {
    if (!running_.load() || bgr.empty()) return;

    cv::Mat preview;
    if (opts_.width > 0 && bgr.cols != opts_.width) {
        const int height = std::max(1, bgr.rows * opts_.width / bgr.cols);
        cv::resize(bgr, preview, cv::Size(opts_.width, height));
    } else {
        preview = bgr;
    }

    std::vector<unsigned char> encoded;
    const std::vector<int> params = {
        cv::IMWRITE_JPEG_QUALITY, std::clamp(opts_.jpeg_quality, 1, 100)};
    if (!cv::imencode(".jpg", preview, encoded, params)) return;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        jpeg_ = std::move(encoded);
        ++sequence_;
    }
    frame_ready_.notify_all();
}

void MjpegServer::stop() {
    if (!running_.exchange(false)) return;
    frame_ready_.notify_all();
    if (listen_fd_ >= 0) {
        shutdown(listen_fd_, SHUT_RDWR);
        close(listen_fd_);
        listen_fd_ = -1;
    }
    if (accept_thread_.joinable()) accept_thread_.join();
    for (std::thread& client : client_threads_) {
        if (client.joinable()) client.join();
    }
    client_threads_.clear();
}

void MjpegServer::acceptLoop() {
    while (running_.load()) {
        const int client = accept(listen_fd_, nullptr, nullptr);
        if (client < 0) {
            if (running_.load()) {
                std::fprintf(stderr, "[stream] accept failed: %s\n",
                             std::strerror(errno));
            }
            break;
        }
        timeval timeout{};
        timeout.tv_sec = 2;
        setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
        client_threads_.emplace_back(&MjpegServer::serveClient, this, client);
    }
}

void MjpegServer::serveClient(int fd) {
    constexpr char kHeader[] =
        "HTTP/1.1 200 OK\r\n"
        "Cache-Control: no-cache, no-store, must-revalidate\r\n"
        "Pragma: no-cache\r\n"
        "Connection: close\r\n"
        "Content-Type: multipart/x-mixed-replace; boundary=lifeguard\r\n\r\n";
    if (!sendAll(fd, kHeader, sizeof(kHeader) - 1)) {
        close(fd);
        return;
    }

    uint64_t delivered = 0;
    while (running_.load()) {
        std::vector<unsigned char> image;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            frame_ready_.wait(lock, [this, delivered] {
                return !running_.load() || sequence_ > delivered;
            });
            if (!running_.load()) break;
            delivered = sequence_;
            image = jpeg_;
        }
        if (image.empty()) continue;

        char part[160];
        const int written = std::snprintf(
            part, sizeof(part),
            "--lifeguard\r\nContent-Type: image/jpeg\r\nContent-Length: %zu\r\n\r\n",
            image.size());
        if (written <= 0 || !sendAll(fd, part, static_cast<size_t>(written)) ||
            !sendAll(fd, image.data(), image.size()) || !sendAll(fd, "\r\n", 2)) {
            break;
        }
    }
    close(fd);
}

}  // namespace lifeguard
