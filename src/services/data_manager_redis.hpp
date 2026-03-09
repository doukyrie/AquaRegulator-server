// 支持 Redis 的遥测服务
// 核心业务逻辑：定期从传感器读取实时数据、从数据库查询历史数据、存储到 Redis 缓存、通过 Publisher 发布给客户端

#pragma once

#include <atomic>
#include <chrono>
#include <memory>
#include <thread>
#include <vector>

#include "core/configuration.hpp"
#include "domain/telemetry_models.hpp"
#include "infrastructure/cache/redis_client.hpp"
#include "infrastructure/cache/redis_telemetry_cache.hpp"
#include "infrastructure/cache/telemetry_cache.hpp"
#include "infrastructure/database/telemetry_repository.hpp"
#include "monitoring/health_monitor.hpp"
#include "infrastructure/sensors/sensor_data.hpp"
#include "transport/tcp_data_sender.hpp"

class TelemetryServiceWithRedis {
public:
    TelemetryServiceWithRedis(const core::PipelineConfig& pipelineConfig,
                              const core::RedisConfig& redisConfig,
                              infrastructure::database::TelemetryRepository& repository,
                              SensorGateway& sensorGateway,
                              TelemetryPublisher& publisher,
                              monitoring::HealthMonitor& healthMonitor)
        : pipelineConfig_(pipelineConfig)
        , redisConfig_(redisConfig)
        , repository_(repository)
        , sensorGateway_(sensorGateway)
        , publisher_(publisher)
        , healthMonitor_(healthMonitor)
        , memoryCache_(pipelineConfig.cacheSize)
        , redisClient_(redisConfig, healthMonitor)
        , redisCache_(redisClient_, pipelineConfig.cacheSize) {

        // 尝试初始化 Redis
        useRedis_ = redisConfig_.enabled && redisClient_.initialize();

        if (useRedis_) {
            LOG_INFO("telemetry_service", "Using Redis cache");
        } else {
            LOG_INFO("telemetry_service", "Using memory cache (Redis disabled or unavailable)");
        }

        // 设置发布器的快照提供者
        publisher_.setSnapshotProvider([this]() {
            std::vector<domain::TelemetryFrame> frames;
            frames.push_back(buildSnapshot(domain::TelemetryChannel::Realtime));
            frames.push_back(buildSnapshot(domain::TelemetryChannel::HistoricalEnvironment));
            frames.push_back(buildSnapshot(domain::TelemetryChannel::HistoricalSoil));
            return frames;
        });
    }

    ~TelemetryServiceWithRedis() {
        stop();
    }

    // 启动服务（启动采样线程）
    void start() {
        if (running_.exchange(true)) {
            return;
        }
        worker_ = std::thread(&TelemetryServiceWithRedis::runLoop, this);
    }

    // 停止服务
    void stop() {
        if (!running_.exchange(false)) {
            return;
        }
        if (worker_.joinable()) {
            worker_.join();
        }
    }

private:
    // 后台线程主循环
    void runLoop() {
        using namespace std::chrono;

        // 上次查询历史数据的时间
        auto lastHistorical = steady_clock::now() - seconds(pipelineConfig_.historicalIntervalSeconds);

        while (running_) {
            auto start = steady_clock::now();

            // 每次都处理实时数据
            processRealtime();

            // 检查是否应该处理历史数据
            if (steady_clock::now() - lastHistorical >= seconds(pipelineConfig_.historicalIntervalSeconds)) {
                processHistorical();
                lastHistorical = steady_clock::now();
            }

            // 精确控制循环周期
            auto elapsed = steady_clock::now() - start;
            auto waitTime = seconds(pipelineConfig_.realtimeIntervalSeconds) - elapsed;

            if (waitTime > seconds(0)) {
                std::this_thread::sleep_for(waitTime);
            }
        }
    }

    // 处理实时数据
    void processRealtime() {
        // 从传感器读取实时数据
        auto reading = sensorGateway_.readRealtime();
        if (!reading.has_value()) {
            healthMonitor_.update("telemetry_service", false, "Realtime read failed");
            return;
        }

        // 存入缓存（Redis 或内存）
        storeToCache(domain::TelemetryChannel::Realtime, *reading);

        // 如果有客户端订阅，发布数据
        if (publisher_.hasSubscribers()) {
            domain::TelemetryFrame frame;
            frame.channel = domain::TelemetryChannel::Realtime;
            frame.snapshot = false;
            frame.correlationId = nextCorrelationId();
            frame.readings.push_back(*reading);
            publisher_.publish(frame);
        }

        healthMonitor_.update("telemetry_service", true, "Realtime frame published");
    }

    // 处理历史数据
    void processHistorical() {
        // 从数据库查询历史环境数据
        auto env = repository_.loadEnvironmental(pipelineConfig_.cacheSize);

        // 从数据库查询历史土壤数据
        auto soil = repository_.loadSoilAndAir(pipelineConfig_.cacheSize);

        // 存入缓存
        for (const auto& reading : env) {
            storeToCache(domain::TelemetryChannel::HistoricalEnvironment, reading);
        }
        for (const auto& reading : soil) {
            storeToCache(domain::TelemetryChannel::HistoricalSoil, reading);
        }

        // 如果有客户端订阅，发布数据
        if (publisher_.hasSubscribers()) {
            if (!env.empty()) {
                auto frame = buildFrame(domain::TelemetryChannel::HistoricalEnvironment, env);
                publisher_.publish(frame);
            }
            if (!soil.empty()) {
                auto frame = buildFrame(domain::TelemetryChannel::HistoricalSoil, soil);
                publisher_.publish(frame);
            }
        }

        healthMonitor_.update("telemetry_service", true, "Historical frame published");
    }

    // 存储到缓存（自动选择 Redis 或内存）
    void storeToCache(domain::TelemetryChannel channel, const domain::TelemetryReading& reading) {
        if (useRedis_) {
            redisCache_.store(channel, reading);
        } else {
            memoryCache_.store(channel, reading);
        }
    }

    // 从缓存获取快照（自动选择 Redis 或内存）
    std::vector<domain::TelemetryReading> snapshotFromCache(domain::TelemetryChannel channel) const {
        if (useRedis_) {
            return redisCache_.snapshot(channel);
        } else {
            return memoryCache_.snapshot(channel);
        }
    }

    // 构建快照 frame（用于新客户端连接）
    domain::TelemetryFrame buildSnapshot(domain::TelemetryChannel channel) const {
        auto readings = snapshotFromCache(channel);
        return buildFrame(channel, readings);
    }

    // 构建普通 frame
    domain::TelemetryFrame buildFrame(domain::TelemetryChannel channel,
                                      const std::vector<domain::TelemetryReading>& readings) const {
        domain::TelemetryFrame frame;
        frame.channel = channel;
        frame.readings = readings;
        frame.snapshot = true;
        frame.correlationId = nextCorrelationId();
        return frame;
    }

    // 生成下一个关联 ID
    std::string nextCorrelationId() const {
        auto id = ++correlationId_;
        return "frame-" + std::to_string(id);
    }

    core::PipelineConfig pipelineConfig_;
    core::RedisConfig redisConfig_;
    infrastructure::database::TelemetryRepository& repository_;
    SensorGateway& sensorGateway_;
    TelemetryPublisher& publisher_;
    monitoring::HealthMonitor& healthMonitor_;

    // 双缓存策略：Redis 优先，内存作为降级方案
    infrastructure::cache::TelemetryCache memoryCache_;
    infrastructure::cache::RedisClient redisClient_;
    infrastructure::cache::RedisTelemetryCache redisCache_;

    bool useRedis_{false};
    std::atomic<bool> running_{false};
    std::thread worker_;
    mutable std::atomic<uint64_t> correlationId_{0};
};
