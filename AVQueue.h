#ifndef AVQUEUE_H
#define AVQUEUE_H

#include <mutex>
#include <condition_variable>
#include <queue>
#include <atomic>
#include <chrono>
#include <optional>

template <typename FrameType>
class AVQueue {
public:
    AVQueue(size_t maxFrames = 100, bool dropOld = false)
        : maxFrames_(maxFrames), isActive_(true), dropOld_(dropOld) {}

    ~AVQueue() {
        setActive(false);
        clear();
    }

    // 添加帧到缓冲区
    void enqueue(FrameType frame) {
        std::unique_lock<std::mutex> lock(mutex_);

        if (!isActive_)
        {
            return;
        }

        if (dropOld_) {
            // 如果满了就丢弃最旧的帧
            if (buffer_.size() >= maxFrames_) {
                buffer_.pop();
            }
        } else {
            // 阻塞直到有空位
            cvFull_.wait(lock, [this] {
                return buffer_.size() < maxFrames_ || !isActive_;
            });

            if (!isActive_)
            {
                return;
            }
        }

        buffer_.push(std::move(frame));
        cvEmpty_.notify_one();
    }

    // 从缓冲区取出帧（支持超时）
    std::optional<FrameType> dequeue(int timeoutMs = 5000) {
        std::unique_lock<std::mutex> lock(mutex_);

        if (!cvEmpty_.wait_for(lock, std::chrono::milliseconds(timeoutMs), [this] {
                return !buffer_.empty() || !isActive_;
            })) {
            return std::nullopt; // 超时
        }

        if (!isActive_ && buffer_.empty()) {
            return std::nullopt;
        }

        auto frame = std::move(buffer_.front());
        buffer_.pop();
        cvFull_.notify_one();

        return frame;
    }

    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        while (!buffer_.empty()) {
            buffer_.pop();
        }
        cvFull_.notify_all();
        cvEmpty_.notify_all();
    }

    void setActive(bool active) {
        std::lock_guard<std::mutex> lock(mutex_);
        isActive_ = active;
        if (!active) {
            cvFull_.notify_all();
            cvEmpty_.notify_all();
        }
    }

    size_t size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return buffer_.size();
    }

private:
    std::queue<FrameType> buffer_;
    mutable std::mutex mutex_;
    std::condition_variable cvFull_;
    std::condition_variable cvEmpty_;
    size_t maxFrames_;
    bool isActive_;
    bool dropOld_; // 是否丢弃旧帧
};

#endif // AVQUEUE_H
