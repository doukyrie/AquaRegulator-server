// 与 Modbus 传感器设备通讯
// 自动重连（如果连接断开）
// 读取实时传感器数据
// 写入控制命令（比如设置阈值）
// 线程安全（多个线程可能同时访问）

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

//传感器（modbus设备数据）
class SensorGateway {
public:
    // 传入配置和健康监控器
    SensorGateway(core::SensorConfig config, monitoring::HealthMonitor& monitor)
        : config_(std::move(config))
        , monitor_(monitor) {
    }

    ~SensorGateway() {
        disconnect();
    }

    // 读取实时传感器数据（直接读modbus）
    // 返回 optional：如果成功返回 reading，失败返回 nullopt
    std::optional<domain::TelemetryReading> readRealtime() 
    {
        std::lock_guard<std::mutex> lk(mutex_);
        if (!ensureConnection()) 
        {
            return std::nullopt;
        }

        // 创建一个 vector 来存放 6 个寄存器的值
        std::vector<uint16_t> registers(config_.registers, 0);
        try 
        {
            // 从地址 0 开始，读取 config_.registers 个寄存器
            modbus_->readRegisters(0, config_.registers, registers.data());
        } 
        catch (const std::exception& ex) 
        {
            // 读取失败
            handleFailure(std::string("readRegisters failed: ") + ex.what());
            return std::nullopt;
        }

        // 创建 TelemetryReading 对象
        domain::TelemetryReading reading;
        reading.label = "Realtime";
        reading.timestamp = currentTimestamp();

        // 假设我们配置读 6 个寄存器，按照以下含义：
        // 寄存器[0] = 土壤 * 100
        // 寄存器[1] = 气体 * 100
        // 寄存器[2] = 雨量 * 100
        // 寄存器[3] = 温度 * 100
        // 寄存器[4] = 湿度 * 100
        // 寄存器[5] = 光照
        if (registers.size() >= 6) {
            reading.soil = registers[0] / 100.0;
            reading.gas = registers[1] / 100.0;
            reading.raindrop = registers[2] / 100.0;
            reading.temperature = registers[3] / 100.0;
            reading.humidity = registers[4] / 100.0;
            reading.light = registers[5] / 100.0;
        }

        // 更新健康状态（成功）
        monitor_.update("sensor_gateway", true, "Realtime sample collected");
        return reading;
    }

    // 写入单个寄存器（控制命令）（封装modbus_tcp中的writeRegister）
    void writeRegister(uint16_t address, uint16_t value) 
    {
        std::lock_guard<std::mutex> lk(mutex_);

        // 确保有一个活跃的 Modbus 连接
        if (!ensureConnection()) 
        {
            return;
        }
        try 
        {
            modbus_->writeRegister(address, value); 
            monitor_.update("sensor_gateway", true, "Register write successful");
        } 
        catch (const std::exception& ex) 
        {
            handleFailure(std::string("writeRegister failed: ") + ex.what());
        }
    }

private:
    // 确保有一个活跃的 Modbus 连接（如果modbus_为空则尝试重连）
    bool ensureConnection() 
    {
        auto now = std::chrono::steady_clock::now();
        if (modbus_) 
        {
            return true;
        }

        // 如果没有连接，且距离上次连接的时间还没到重试的时间，则返回false
        if (now - lastAttempt_ < std::chrono::seconds(config_.retrySeconds)) 
        {
            return false;
        }

        //开始重连
        lastAttempt_ = now;

        try 
        {
            //创建modbus句柄
            modbus_ = std::make_unique<ModbusTCP>(config_.endpoint.c_str(), config_.port);
            //建立连接
            modbus_->connect();
            //更新健康检查
            monitor_.update("sensor_gateway", true, "Modbus connected");
            LOG_INFO("sensor_gateway", "Connected to Modbus sensor at ", config_.endpoint, ":", config_.port);
            return true;
        } 
        catch (const std::exception& ex) 
        {    //连接失败
            modbus_.reset();    //释放失败的连接
            handleFailure(std::string("Connection error: ") + ex.what());
            return false;
        }
    }

    //释放连接
    void disconnect() 
    {
        std::lock_guard<std::mutex> lk(mutex_);
        modbus_.reset();
    }

    // 处理故障（记录日志和更新健康状态）
    void handleFailure(const std::string& reason) 
    {
        LOG_WARN("sensor_gateway", reason);
        monitor_.update("sensor_gateway", false, reason);
    }

    // 获取当前时间的字符串表示
    std::string currentTimestamp() const 
    {
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

    core::SensorConfig config_; // Modbus 配置
    monitoring::HealthMonitor& monitor_;    // 健康监控器引用
    std::unique_ptr<ModbusTCP> modbus_; // Modbus 连接（智能指针自动管理）
    std::mutex mutex_;
    std::chrono::steady_clock::time_point lastAttempt_{};   // 上次尝试连接的时间
};

// 自动重连机制
// 时刻 1: 初始化
//   modbus_ = nullptr
//   lastAttempt_ = 0

// 时刻 2: 第一次 readRealtime() 调用
//   ensureConnection() {
//     now = steady_clock::now()
//     modbus_ == nullptr ✓
//     now - lastAttempt_ >= retrySeconds ✓
    
//     尝试连接 → 成功 ✓
//     modbus_ = std::make_unique<ModbusTCP>(...)
//   }
  
// 时刻 3: Modbus 设备断电（连接突然断开）

// 时刻 4: 第二次 readRealtime() 调用（立即）
//   ensureConnection() {
//     modbus_ 仍然指向坏的连接
//     但 readRealtime() 调用 modbus_->readRegisters() 会抛异常
//     catch 捕获异常
//     handleFailure() 记录错误
//   }

// 时刻 5: 第三次 readRealtime() 调用（1 秒后）
//   ensureConnection() {
//     now - lastAttempt_ < 5秒 ✓
//     return false  ← 不尝试重连，避免频繁尝试
//   }

// 时刻 6: 第四次 readRealtime() 调用（6 秒后，超过 retrySeconds）
//   ensureConnection() {
//     now - lastAttempt_ >= 5秒 ✓
//     lastAttempt_ = now  ← 更新尝试时间
    
//     modbus_.reset()  ← 释放坏的连接
//     尝试新连接 → 成功 ✓
//     modbus_ = std::make_unique<ModbusTCP>(...)
//   }