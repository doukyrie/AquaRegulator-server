// 缓存传感器数据到内存中，避免频繁访问数据库。采用 LRU 策略（当缓存满时，删除最旧的数据）

#pragma once

#include <deque>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "domain/telemetry_models.hpp"

namespace infrastructure::cache {

class TelemetryCache {
public:
    // 指定每个通道的缓存容量
    explicit TelemetryCache(std::size_t capacityPerChannel)
        : capacity_(capacityPerChannel) {}

    // 存储一条读数到特定通道的缓存
    void store(domain::TelemetryChannel channel, const domain::TelemetryReading& reading) {
        std::lock_guard<std::mutex> lk(mutex_);

        auto& buffer = cache_[channel]; // 获取该通道的缓冲区
        buffer.push_back(reading);  // 添加到尾部

        // LRU：如果超过容量，删除最旧的（最前面的）
        if (buffer.size() > capacity_) {
            buffer.pop_front();
        }
    }

    // 获取特定通道的所有缓存数据（快照）（将队列的数据取出来放进vector）
    std::vector<domain::TelemetryReading> snapshot(domain::TelemetryChannel channel) const {
        std::lock_guard<std::mutex> lk(mutex_);
        std::vector<domain::TelemetryReading> copy;

        // 查找该通道是否有缓存
        if (auto it = cache_.find(channel); it != cache_.end()) {
            // 把 deque 复制成 vector
            copy.assign(it->second.begin(), it->second.end());
        }
        return copy;    // 返回副本（不是引用，避免 mutex 保护问题）
    }

    // 获取所有通道的所有缓存数据（所有通道的deque中的数据都塞进一个vector里，无法分类）
    std::vector<domain::TelemetryReading> snapshotAll() const {
        std::lock_guard<std::mutex> lk(mutex_);
        std::vector<domain::TelemetryReading> copy;

        // [...] (结构化绑定)：它直接把 pair 里的 first（键）和 second（值）拆解出来，并赋予它们名字
        // 用下划线表示“这个变量我占个位，但我不打算用它”
        for (const auto& [_, buffer] : cache_) {
            // copy.end()：告诉 vector 从哪里开始插（末尾）。
            // buffer.begin(), buffer.end()：定义了要插入的数据范围（整个队列）
            copy.insert(copy.end(), buffer.begin(), buffer.end());
        }
        return copy;
    }

private:
    std::size_t capacity_;  // 遥测通道的缓存容量
    mutable std::mutex mutex_;

    //每个通道（数据类型）维护一个队列存TelemetryReading数据
    std::unordered_map<domain::TelemetryChannel, std::deque<domain::TelemetryReading>, domain::TelemetryChannelHash> cache_;
};

} // namespace infrastructure::cache

// 数据结构说明
// TelemetryCache:

// cache_ (unordered_map)
//   │
//   ├─ Realtime → deque [reading1, reading2, reading3, ...]
//   │                    │
//   │                    ├─ 最旧（会被删除）
//   │                    └─ 最新
//   │
//   ├─ HistoricalEnvironment → deque [...]
//   │
//   └─ HistoricalSoil → deque [...]

// 当 cache[Realtime].size() > capacity（比如 > 120）：
//   pop_front() → 删除最旧的

