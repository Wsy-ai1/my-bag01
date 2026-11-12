#pragma once
/**
 * latest_frame.hpp —— 容量为 1 的线程安全帧队列
 *
 * 取图线程一直往里塞，识别线程随时取"最新"的一帧；
 * 队列满时旧帧被覆盖（项目里的队列容量就是 1，避免处理过期画面）。
 */

#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <optional>
#include <utility>

namespace io {

template <typename T>
class LatestFrame {
public:
    /** 生产者：放入一帧，覆盖尚未被取走的旧帧 */
    void push(T value) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (closed_) {
                return;
            }
            if (slot_.has_value()) {
                ++dropped_;
            }
            slot_ = std::move(value);
        }
        cv_.notify_one();
    }

    /** 消费者：最多等 timeout_ms，取走最新一帧 */
    bool waitPop(T& out, int timeout_ms) {
        std::unique_lock<std::mutex> lock(mutex_);
        const bool ready = cv_.wait_for(lock, std::chrono::milliseconds(timeout_ms),
                                        [this] { return slot_.has_value() || closed_; });
        if (!ready || !slot_.has_value()) {
            return false;
        }
        out = std::move(*slot_);
        slot_.reset();
        return true;
    }

    /** 唤醒所有等待者并停止接收新帧 */
    void close() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            closed_ = true;
        }
        cv_.notify_all();
    }

    std::size_t dropped() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return dropped_;
    }

private:
    mutable std::mutex      mutex_;
    std::condition_variable cv_;
    std::optional<T>        slot_;
    std::size_t             dropped_ = 0;
    bool                    closed_  = false;
};

}  // namespace io
