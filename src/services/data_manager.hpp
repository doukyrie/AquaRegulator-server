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

    void start() {
        if (running_.exchange(true)) {
            return;
        }
        worker_ = std::thread(&TelemetryService::runLoop, this);
    }

    void stop() {
        if (!running_.exchange(false)) {
            return;
        }
        if (worker_.joinable()) {
            worker_.join();
        }
    }

private:
    void runLoop() {
        using namespace std::chrono;
        auto lastHistorical = steady_clock::now() - seconds(pipelineConfig_.historicalIntervalSeconds);

        while (running_) {
            auto start = steady_clock::now();
            processRealtime();

            if (steady_clock::now() - lastHistorical >= seconds(pipelineConfig_.historicalIntervalSeconds)) {
                processHistorical();
                lastHistorical = steady_clock::now();
            }

            auto elapsed = steady_clock::now() - start;
            auto waitTime = seconds(pipelineConfig_.realtimeIntervalSeconds) - elapsed;
            if (waitTime > seconds(0)) {
                std::this_thread::sleep_for(waitTime);
            }
        }
    }

    void processRealtime() {
        auto reading = sensorGateway_.readRealtime();
        if (!reading.has_value()) {
            healthMonitor_.update("telemetry_service", false, "Realtime read failed");
            return;
        }

        cache_.store(domain::TelemetryChannel::Realtime, *reading);

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

    void processHistorical() {
        auto env = repository_.loadEnvironmental(pipelineConfig_.cacheSize);
        auto soil = repository_.loadSoilAndAir(pipelineConfig_.cacheSize);

        for (const auto& reading : env) {
            cache_.store(domain::TelemetryChannel::HistoricalEnvironment, reading);
        }
        for (const auto& reading : soil) {
            cache_.store(domain::TelemetryChannel::HistoricalSoil, reading);
        }

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

    domain::TelemetryFrame buildSnapshot(domain::TelemetryChannel channel) const {
        auto readings = cache_.snapshot(channel);
        return buildFrame(channel, readings);
    }

    domain::TelemetryFrame buildFrame(domain::TelemetryChannel channel,
                                      const std::vector<domain::TelemetryReading>& readings) const {
        domain::TelemetryFrame frame;
        frame.channel = channel;
        frame.readings = readings;
        frame.snapshot = true;
        frame.correlationId = nextCorrelationId();
        return frame;
    }

    std::string nextCorrelationId() const {
        auto id = ++correlationId_;
        return "frame-" + std::to_string(id);
    }

    core::PipelineConfig pipelineConfig_;
    infrastructure::database::TelemetryRepository& repository_;
    SensorGateway& sensorGateway_;
    TelemetryPublisher& publisher_;
    monitoring::HealthMonitor& healthMonitor_;
    infrastructure::cache::TelemetryCache cache_;

    std::atomic<bool> running_{false};
    std::thread worker_;
    mutable std::atomic<uint64_t> correlationId_{0};
};
