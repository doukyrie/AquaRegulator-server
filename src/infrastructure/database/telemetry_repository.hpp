// TelemetryRepository（数据库访问层）

#pragma once

#include <vector>

#include "core/configuration.hpp"
#include "domain/telemetry_models.hpp"
#include "infrastructure/database/mariadb_client.hpp"

namespace infrastructure::database {

//mysql数据库类（数据通过TelemetryReading类型传输）
class TelemetryRepository {
public:
    TelemetryRepository() = default;

    // 初始化：创建 MariaDbClient 并连接
    bool initialize(const core::DatabaseConfig& cfg);

    // 查询历史环境数据（温度、湿度、光照）
    std::vector<domain::TelemetryReading> loadEnvironmental(std::size_t limit);

    // 查询历史土壤数据（土壤、气体、雨量）
    std::vector<domain::TelemetryReading> loadSoilAndAir(std::size_t limit);

    // 检查并重建连接（如果连接断开）
    void refreshConnection();

private:
    // 辅助函数：将 MYSQL_ROW 转换为 TelemetryReading
    domain::TelemetryReading buildEnvReading(MYSQL_ROW row) const;
    domain::TelemetryReading buildSoilReading(MYSQL_ROW row) const;

    core::DatabaseConfig config_;   // 保存配置
    MariaDbClient client_;  // 数据库连接对象
};

} // namespace infrastructure::database
