#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <thread>
#include <vector>

#include <opencv2/core.hpp>

namespace lifeguard {

// Minimal HTTP multipart-MJPEG server for a browser-based live preview.
class MjpegServer {
public:
    struct Options {
        int port = 8080;
        int width = 960;       // 0 preserves the source width
        int jpeg_quality = 80;
    };

    explicit MjpegServer(Options opts);
    ~MjpegServer();

    bool start();
    void publish(const cv::Mat& bgr);
    void stop();

private:
    void acceptLoop();
    void serveClient(int fd);

    Options opts_;
    std::atomic<bool> running_{false};
    int listen_fd_ = -1;
    std::thread accept_thread_;
    std::vector<std::thread> client_threads_;
    std::mutex mutex_;
    std::condition_variable frame_ready_;
    std::vector<unsigned char> jpeg_;
    uint64_t sequence_ = 0;
};

}  // namespace lifeguard
