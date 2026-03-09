// 基于 Redis 的遥测数据缓存
// 替代原有的内存缓存，支持分布式部署和数据持久化

#pragma once

#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "domain/telemetry_models.hpp"
#include "infrastructure/cache/redis_client.hpp"
#include "core/logger.hpp"

namespace infrastructure::cache {

class RedisTelemetryCache {
public:
    explicit RedisTelemetryCache(RedisClient& redisClient, std::size_t capacityPerChannel)
        : redis_(redisClient)
        , capacity_(capacityPerChannel) {
    }

    // 存储一条读数到特定通道的缓存
    void store(domain::TelemetryChannel channel, const domain::TelemetryReading& reading) {
        // 构造 Redis key
        std::string key = buildKey(channel);

        // 序列化为 JSON
        auto json = domain::toJson(reading);
        std::string value = json.dump();

        // LPUSH：将数据推入列表头部
        if (!redis_.lpush(key, value)) {
            LOG_WARN("redis_telemetry_cache", "Failed to store reading to Redis");
            return;
        }

        // LTRIM：保留最新的 capacity_ 条数据
        redis_.ltrim(key, 0, static_cast<long long>(capacity_) - 1);

        // 设置过期时间（1 小时）
        redis_.expire(key, std::chrono::seconds(3600));
    }

    // 获取特定通道的所有缓存数据（快照）
    std::vector<domain::TelemetryReading> snapshot(domain::TelemetryChannel channel) const {
        std::string key = buildKey(channel);

        // LRANGE：获取列表中的所有元素
        auto values = redis_.lrange(key, 0, -1);

        std::vector<domain::TelemetryReading> readings;
        readings.reserve(values.size());

        // 反序列化 JSON
        for (const auto& value : values) {
            try {
                auto json = nlohmann::json::parse(value);
                readings.push_back(domain::fromJson(json));
            } catch (const std::exception& ex) {
                LOG_WARN("redis_telemetry_cache", "Failed to parse reading: ", ex.what());
            }
        }

        return readings;
    }

    // 获取所有通道的所有缓存数据
    std::vector<domain::TelemetryReading> snapshotAll() const {
        std::vector<domain::TelemetryReading> allReadings;

        // 遍历所有通道
        for (auto channel : {domain::TelemetryChannel::Realtime,
                             domain::TelemetryChannel::HistoricalEnvironment,
                             domain::TelemetryChannel::HistoricalSoil}) {
            auto readings = snapshot(channel);
            allReadings.insert(allReadings.end(), readings.begin(), readings.end());
        }

        return allReadings;
    }

private:
    // 构造 Redis key
    std::string buildKey(domain::TelemetryChannel channel) const {
        switch (channel) {
            case domain::TelemetryChannel::Realtime:
                return "telemetry:realtime";
            case domain::TelemetryChannel::HistoricalEnvironment:
                return "telemetry:historical:env";
            case domain::TelemetryChannel::HistoricalSoil:
                return "telemetry:historical:soil";
            default:
                return "telemetry:unknown";
        }
    }

    RedisClient& redis_;
    std::size_t capacity_;
};

} // namespace infrastructure::cache
