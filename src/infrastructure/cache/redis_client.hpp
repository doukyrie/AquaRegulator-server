// Redis 客户端封装
// 提供连接池管理、自动重连、异常处理和健康检查

#pragma once

#include <chrono>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include <sw/redis++/redis.h>

#include "core/configuration.hpp"
#include "core/logger.hpp"
#include "monitoring/health_monitor.hpp"

namespace infrastructure::cache {

class RedisClient {
public:
    RedisClient(const core::RedisConfig& config, monitoring::HealthMonitor& monitor)
        : config_(config)
        , monitor_(monitor) {
    }

    ~RedisClient() {
        disconnect();
    }

    // 初始化连接
    bool initialize() {
        if (!config_.enabled) {
            LOG_INFO("redis_client", "Redis is disabled in configuration");
            return false;
        }

        std::lock_guard<std::mutex> lk(mutex_);
        return ensureConnection();
    }

    // 设置字符串值
    bool set(const std::string& key, const std::string& value, std::chrono::seconds ttl = std::chrono::seconds(0)) {
        std::lock_guard<std::mutex> lk(mutex_);
        if (!ensureConnection()) {
            return false;
        }

        try {
            if (ttl.count() > 0) {
                redis_->setex(key, ttl.count(), value);
            } else {
                redis_->set(key, value);
            }
            monitor_.update("redis_client", true, "SET operation successful");
            return true;
        } catch (const sw::redis::Error& ex) {
            handleFailure(std::string("SET failed: ") + ex.what());
            return false;
        }
    }

    // 获取字符串值
    std::optional<std::string> get(const std::string& key) {
        std::lock_guard<std::mutex> lk(mutex_);
        if (!ensureConnection()) {
            return std::nullopt;
        }

        try {
            auto val = redis_->get(key);
            if (val) {
                monitor_.update("redis_client", true, "GET operation successful");
                return *val;
            }
            return std::nullopt;
        } catch (const sw::redis::Error& ex) {
            handleFailure(std::string("GET failed: ") + ex.what());
            return std::nullopt;
        }
    }

    // 列表左侧推入（LPUSH）
    bool lpush(const std::string& key, const std::string& value) {
        std::lock_guard<std::mutex> lk(mutex_);
        if (!ensureConnection()) {
            return false;
        }

        try {
            redis_->lpush(key, value);
            monitor_.update("redis_client", true, "LPUSH operation successful");
            return true;
        } catch (const sw::redis::Error& ex) {
            handleFailure(std::string("LPUSH failed: ") + ex.what());
            return false;
        }
    }

    // 列表修剪（LTRIM）- 保留指定范围的元素
    bool ltrim(const std::string& key, long long start, long long stop) {
        std::lock_guard<std::mutex> lk(mutex_);
        if (!ensureConnection()) {
            return false;
        }

        try {
            redis_->ltrim(key, start, stop);
            return true;
        } catch (const sw::redis::Error& ex) {
            handleFailure(std::string("LTRIM failed: ") + ex.what());
            return false;
        }
    }

    // 获取列表范围（LRANGE）
    std::vector<std::string> lrange(const std::string& key, long long start, long long stop) {
        std::lock_guard<std::mutex> lk(mutex_);
        if (!ensureConnection()) {
            return {};
        }

        try {
            std::vector<std::string> result;
            redis_->lrange(key, start, stop, std::back_inserter(result));
            monitor_.update("redis_client", true, "LRANGE operation successful");
            return result;
        } catch (const sw::redis::Error& ex) {
            handleFailure(std::string("LRANGE failed: ") + ex.what());
            return {};
        }
    }

    // 设置过期时间
    bool expire(const std::string& key, std::chrono::seconds ttl) {
        std::lock_guard<std::mutex> lk(mutex_);
        if (!ensureConnection()) {
            return false;
        }

        try {
            redis_->expire(key, ttl.count());
            return true;
        } catch (const sw::redis::Error& ex) {
            handleFailure(std::string("EXPIRE failed: ") + ex.what());
            return false;
        }
    }

    // 发布消息（Pub/Sub）
    bool publish(const std::string& channel, const std::string& message) {
        std::lock_guard<std::mutex> lk(mutex_);
        if (!ensureConnection()) {
            return false;
        }

        try {
            redis_->publish(channel, message);
            monitor_.update("redis_client", true, "PUBLISH operation successful");
            return true;
        } catch (const sw::redis::Error& ex) {
            handleFailure(std::string("PUBLISH failed: ") + ex.what());
            return false;
        }
    }

    // 检查连接状态
    bool isConnected() const {
        std::lock_guard<std::mutex> lk(mutex_);
        return redis_ != nullptr;
    }

private:
    // 确保连接可用
    bool ensureConnection() {
        auto now = std::chrono::steady_clock::now();

        if (redis_) {
            // 测试连接是否有效
            try {
                redis_->ping();
                return true;
            } catch (const sw::redis::Error&) {
                // 连接失效，重置
                redis_.reset();
            }
        }

        // 检查重试间隔
        if (now - lastAttempt_ < std::chrono::seconds(5)) {
            return false;
        }

        lastAttempt_ = now;

        try {
            // 构建连接选项
            sw::redis::ConnectionOptions opts;
            opts.host = config_.host;
            opts.port = config_.port;
            opts.password = config_.password;
            opts.db = config_.database;
            opts.socket_timeout = std::chrono::milliseconds(config_.timeoutMs);

            // 连接池选项
            sw::redis::ConnectionPoolOptions pool_opts;
            pool_opts.size = config_.poolSize;

            // 创建 Redis 连接
            redis_ = std::make_unique<sw::redis::Redis>(opts, pool_opts);

            // 测试连接
            redis_->ping();

            monitor_.update("redis_client", true, "Redis connected");
            LOG_INFO("redis_client", "Connected to Redis at ", config_.host, ":", config_.port);
            return true;
        } catch (const sw::redis::Error& ex) {
            redis_.reset();
            handleFailure(std::string("Connection error: ") + ex.what());
            return false;
        }
    }

    // 断开连接
    void disconnect() {
        std::lock_guard<std::mutex> lk(mutex_);
        redis_.reset();
    }

    // 处理故障
    void handleFailure(const std::string& reason) {
        LOG_WARN("redis_client", reason);
        monitor_.update("redis_client", false, reason);
    }

    core::RedisConfig config_;
    monitoring::HealthMonitor& monitor_;
    std::unique_ptr<sw::redis::Redis> redis_;
    mutable std::mutex mutex_;
    std::chrono::steady_clock::time_point lastAttempt_{};
};

} // namespace infrastructure::cache

