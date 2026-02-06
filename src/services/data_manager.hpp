// 这是整个项目的核心业务逻辑。负责：

// 定期从传感器读取实时数据
// 定期从数据库查询历史数据
// 存储到缓存
// 通过 Publisher 发布给客户端

#pragma once

#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

#include "core/configuration.hpp"
#include "domain/telemetry_models.hpp"
#include "infrastructure/cache/telemetry_cache.hpp"
#include "infrastructure/database/telemetry_repository.hpp"
#include "monitoring/health_monitor.hpp"
#include "infrastructure/sensors/sensor_data.hpp"
#include "transport/tcp_data_sender.hpp"

class TelemetryService {
public:
    TelemetryService(const core::PipelineConfig& pipelineConfig,
                     infrastructure::database::TelemetryRepository& repository,
                     SensorGateway& sensorGateway,
                     TelemetryPublisher& publisher,
                     monitoring::HealthMonitor& healthMonitor)
        : pipelineConfig_(pipelineConfig)
        , repository_(repository)
        , sensorGateway_(sensorGateway)
        , publisher_(publisher)
        , healthMonitor_(healthMonitor)
        , cache_(pipelineConfig.cacheSize) {

        // 设置发布器的快照提供者
        publisher_.setSnapshotProvider([this]() {
            std::vector<domain::TelemetryFrame> frames;
            frames.push_back(buildSnapshot(domain::TelemetryChannel::Realtime));
            frames.push_back(buildSnapshot(domain::TelemetryChannel::HistoricalEnvironment));
            frames.push_back(buildSnapshot(domain::TelemetryChannel::HistoricalSoil));
            return frames;
        });
    }

    ~TelemetryService() {
        stop();
    }

    // 启动服务（启动采样线程）
    void start() 
    {
        if (running_.exchange(true)) {
            return;
        }
        worker_ = std::thread(&TelemetryService::runLoop, this);
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

        // 上次查询历史数据的时间（因为此时是第一次进入runLoop，第一次查询数据（之前没查过），所以默认上次查询是30秒前，保证第一次进入循环一定会查历史数据）
        auto lastHistorical = steady_clock::now() - seconds(pipelineConfig_.historicalIntervalSeconds);

        while (running_) {
            // 记录循环开始时间
            auto start = steady_clock::now();

            // 每次都处理实时数据（间隔5秒在下面的逻辑实现）
            processRealtime();

            // 检查是否应该处理历史数据
            if (steady_clock::now() - lastHistorical >= seconds(pipelineConfig_.historicalIntervalSeconds)) {
                processHistorical();    //处理历史数据
                lastHistorical = steady_clock::now();
            }

            // 精确控制循环周期
            auto elapsed = steady_clock::now() - start; //处理业务逻辑（实时数据+历史数据）实际消耗的时间
            auto waitTime = seconds(pipelineConfig_.realtimeIntervalSeconds) - elapsed; //理想周期是 5 秒，处理用了 1 秒，那么 waitTime 就是 4 秒

            // 如果某次处理非常耗时（比如 elapsed 用了 6 秒，而周期只有 5 秒），waitTime 就会变成负数，代码就不会进入 sleep，而是立刻开始下一次循环
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

        // 将实时数据存入缓存
        cache_.store(domain::TelemetryChannel::Realtime, *reading);

        // 如果有客户端订阅（连接），发布数据
        if (publisher_.hasSubscribers()) {
            domain::TelemetryFrame frame;
            frame.channel = domain::TelemetryChannel::Realtime; //数据的通道（实时、历史环境、历史土壤）
            frame.snapshot = false; // 增量数据，不是快照
            frame.correlationId = nextCorrelationId();
            frame.readings.push_back(*reading); 
            publisher_.publish(frame);  //对所有连接的客户端发布数据
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
            cache_.store(domain::TelemetryChannel::HistoricalEnvironment, reading);
        }
        for (const auto& reading : soil) {
            cache_.store(domain::TelemetryChannel::HistoricalSoil, reading);
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

    // 构建快照 frame（用于新客户端连接）
    domain::TelemetryFrame buildSnapshot(domain::TelemetryChannel channel) const {
        auto readings = cache_.snapshot(channel);
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

    core::PipelineConfig pipelineConfig_;   // 数据采集管道配置（实时/历史数据的采集间隔、缓存大小）
    infrastructure::database::TelemetryRepository& repository_; //数据库
    SensorGateway& sensorGateway_;  //传感器
    TelemetryPublisher& publisher_; //推送端
    monitoring::HealthMonitor& healthMonitor_;  //健康监控
    infrastructure::cache::TelemetryCache cache_;   //缓存传感器数据到内存中，避免频繁访问数据库

    std::atomic<bool> running_{false};
    std::thread worker_;
    mutable std::atomic<uint64_t> correlationId_{0};    //关联id
};
