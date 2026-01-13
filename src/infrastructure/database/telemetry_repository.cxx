#include "infrastructure/database/telemetry_repository.hpp"

#include <algorithm>
#include <iomanip>
#include <sstream>

#include "core/logger.hpp"

namespace infrastructure::database {

bool TelemetryRepository::initialize(const core::DatabaseConfig& cfg) {
    config_ = cfg;
    if (!client_.initialize()) {
        return false;
    }
    return client_.connect(cfg);
}

void TelemetryRepository::refreshConnection() {
    if (client_.isConnected() && client_.ping()) {
        return;
    }
    LOG_WARN("telemetry_repo", "Refreshing MariaDB connection...");
    client_.disconnect();
    client_.initialize();
    client_.connect(config_);
}

std::vector<domain::TelemetryReading> TelemetryRepository::loadEnvironmental(std::size_t limit) {
    refreshConnection();

    std::ostringstream oss;
    oss << "SELECT time, temperature, humidity, light "
        << "FROM environmental_conditions "
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
        readings.emplace_back(buildEnvReading(row));
    }
    mysql_free_result(res);

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
    reading.timestamp = row[0] ? row[0] : "N/A";
    reading.temperature = row[1] ? std::stod(row[1]) : 0.0;
    reading.humidity = row[2] ? std::stod(row[2]) : 0.0;
    reading.light = row[3] ? std::stod(row[3]) : 0.0;
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
