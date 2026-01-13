#pragma once

#include <chrono>
#include <ctime>
#include <iomanip>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "core/configuration.hpp"
#include "core/logger.hpp"
#include "domain/telemetry_models.hpp"
#include "modbus_tcp.hpp"
#include "monitoring/health_monitor.hpp"

class SensorGateway {
public:
    SensorGateway(core::SensorConfig config, monitoring::HealthMonitor& monitor)
        : config_(std::move(config))
        , monitor_(monitor) {
    }

    ~SensorGateway() {
        disconnect();
    }

    std::optional<domain::TelemetryReading> readRealtime() {
        std::lock_guard<std::mutex> lk(mutex_);
        if (!ensureConnection()) {
            return std::nullopt;
        }

        std::vector<uint16_t> registers(config_.registers, 0);
        try {
            modbus_->readRegisters(0, config_.registers, registers.data());
        } catch (const std::exception& ex) {
            handleFailure(std::string("readRegisters failed: ") + ex.what());
            return std::nullopt;
        }

        domain::TelemetryReading reading;
        reading.label = "Realtime";
        reading.timestamp = currentTimestamp();
        if (registers.size() >= 6) {
            reading.soil = registers[0] / 100.0;
            reading.gas = registers[1] / 100.0;
            reading.raindrop = registers[2] / 100.0;
            reading.temperature = registers[3] / 100.0;
            reading.humidity = registers[4] / 100.0;
            reading.light = registers[5] / 100.0;
        }

        monitor_.update("sensor_gateway", true, "Realtime sample collected");
        return reading;
    }

    void writeRegister(uint16_t address, uint16_t value) {
        std::lock_guard<std::mutex> lk(mutex_);
        if (!ensureConnection()) {
            return;
        }
        try {
            modbus_->writeRegister(address, value);
            monitor_.update("sensor_gateway", true, "Register write successful");
        } catch (const std::exception& ex) {
            handleFailure(std::string("writeRegister failed: ") + ex.what());
        }
    }

private:
    bool ensureConnection() {
        auto now = std::chrono::steady_clock::now();
        if (modbus_) {
            return true;
        }

        if (now - lastAttempt_ < std::chrono::seconds(config_.retrySeconds)) {
            return false;
        }
        lastAttempt_ = now;

        try {
            modbus_ = std::make_unique<ModbusTCP>(config_.endpoint.c_str(), config_.port);
            modbus_->connect();
            monitor_.update("sensor_gateway", true, "Modbus connected");
            LOG_INFO("sensor_gateway", "Connected to Modbus sensor at ", config_.endpoint, ":", config_.port);
            return true;
        } catch (const std::exception& ex) {
            modbus_.reset();
            handleFailure(std::string("Connection error: ") + ex.what());
            return false;
        }
    }

    void disconnect() {
        std::lock_guard<std::mutex> lk(mutex_);
        modbus_.reset();
    }

    void handleFailure(const std::string& reason) {
        LOG_WARN("sensor_gateway", reason);
        monitor_.update("sensor_gateway", false, reason);
    }

    std::string currentTimestamp() const {
        auto now = std::chrono::system_clock::now();
        std::time_t t = std::chrono::system_clock::to_time_t(now);
        std::tm tm{};
#ifdef _WIN32
        localtime_s(&tm, &t);
#else
        localtime_r(&t, &tm);
#endif
        std::ostringstream oss;
        oss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
        return oss.str();
    }

    core::SensorConfig config_;
    monitoring::HealthMonitor& monitor_;
    std::unique_ptr<ModbusTCP> modbus_;
    std::mutex mutex_;
    std::chrono::steady_clock::time_point lastAttempt_{};
};
