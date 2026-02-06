#include "infrastructure/database/telemetry_repository.hpp"

#include <algorithm>
#include <iomanip>
#include <sstream>

#include "core/logger.hpp"

namespace infrastructure::database {

bool TelemetryRepository::initialize(const core::DatabaseConfig& cfg) {
    config_ = cfg;

    // 初始化 MySQL 客户端
    if (!client_.initialize()) {
        return false;
    }
    return client_.connect(cfg);    // 连接到数据库
}

void TelemetryRepository::refreshConnection() {
    // 检查连接是否仍然活跃
    if (client_.isConnected() && client_.ping()) {  //handle没被释放且能ping通
        return;
    }
    LOG_WARN("telemetry_repo", "Refreshing MariaDB connection...");

    // 连接断开就重连
    client_.disconnect();   //关闭连接
    client_.initialize();   //初始化
    client_.connect(config_);   //重连
}

// 查询历史环境数据（温度、湿度、光照）
std::vector<domain::TelemetryReading> TelemetryRepository::loadEnvironmental(std::size_t limit) {
    // 刷新连接（如果中断）
    refreshConnection();

    //构造sql查询
    std::ostringstream oss;
    oss << "SELECT time, temperature, humidity, light "
        << "FROM environmental_conditions "
        << "ORDER BY time DESC LIMIT " << limit;

    //执行查询
    if (!client_.execute(oss.str())) {
        return {};  // 查询失败，返回空数组
    }

    // 获取结果集
    MYSQL_RES* res = client_.storeResult();
    if (res == nullptr) {
        LOG_ERROR("telemetry_repo", "mysql_store_result() returned null");
        return {};
    }

    // 逐行读取结果
    std::vector<domain::TelemetryReading> readings;
    MYSQL_ROW row;
    while ((row = mysql_fetch_row(res)) != nullptr) {
        readings.emplace_back(buildEnvReading(row));
    }

    // 释放结果集
    mysql_free_result(res);

    // 反转数组（因为 ORDER BY time DESC 得到的是最新的在前）
    // 反转后得到时间从早到晚的顺序
    std::reverse(readings.begin(), readings.end());
    return readings;
}

std::vector<domain::TelemetryReading> TelemetryRepository::loadSoilAndAir(std::size_t limit) {
    refreshConnection();

    std::ostringstream oss;
    oss << "SELECT time, soil, gas, raindrop "
        << "FROM soil_and_air_quality "
        << "ORDER BY time DESC LIMIT " << limit;

    if (!client_.execute(oss.str())) {
        return {};
    }

    MYSQL_RES* res = client_.storeResult();
    if (res == nullptr) {
        LOG_ERROR("telemetry_repo", "mysql_store_result() returned null");
        return {};
    }

    std::vector<domain::TelemetryReading> readings;
    MYSQL_ROW row;
    while ((row = mysql_fetch_row(res)) != nullptr) {
        readings.emplace_back(buildSoilReading(row));
    }
    mysql_free_result(res);

    std::reverse(readings.begin(), readings.end());
    return readings;
}

domain::TelemetryReading TelemetryRepository::buildEnvReading(MYSQL_ROW row) const {
    domain::TelemetryReading reading;

    reading.label = "Historical_ENV";
    reading.timestamp = row[0] ? row[0] : "N/A";    // 如果 row[0] 为 NULL，用 "N/A"

    // row[1] = temperature（字符串）→ 转为 double
    reading.temperature = row[1] ? std::stod(row[1]) : 0.0;
    reading.humidity = row[2] ? std::stod(row[2]) : 0.0;
    reading.light = row[3] ? std::stod(row[3]) : 0.0;

    // 其他字段保持默认值 0.0
    // reading.soil, gas, raindrop 都是 0.0

    return reading;
}

domain::TelemetryReading TelemetryRepository::buildSoilReading(MYSQL_ROW row) const {
    domain::TelemetryReading reading;
    reading.label = "Historical_Soil";
    reading.timestamp = row[0] ? row[0] : "N/A";
    reading.soil = row[1] ? std::stod(row[1]) : 0.0;
    reading.gas = row[2] ? std::stod(row[2]) : 0.0;
    reading.raindrop = row[3] ? std::stod(row[3]) : 0.0;
    return reading;
}

} // namespace infrastructure::database
