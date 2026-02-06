#include "infrastructure/database/mariadb_client.hpp"

#include <iostream>

#include "core/logger.hpp"

namespace infrastructure::database {

// 默认构造函数
MariaDbClient::MariaDbClient() = default;

// 析构函数：确保连接被关闭，避免资源泄漏
MariaDbClient::~MariaDbClient() {
    disconnect();
}

// 创建 MySQL 连接句柄
// 这一步不会建立真实连接，只是初始化结构体
bool MariaDbClient::initialize() {
    handle_ = mysql_init(nullptr);
    if (handle_ == nullptr) {
        LOG_ERROR("database", "mysql_init() failed");
        return false;
    }
    return true;
}

// 建立真实连接
bool MariaDbClient::connect(const core::DatabaseConfig& cfg) {
    config_ = cfg;  //保存数据库配置到自身成员属性

    // 当handle为空的时候先初始化，初始化失败返回false
    if (handle_ == nullptr && !initialize()) {
        return false;
    }

    // 建立真实的 TCP 连接到数据库
    MYSQL* result = mysql_real_connect(
        handle_,
        cfg.host.c_str(),
        cfg.user.c_str(),
        cfg.password.c_str(),
        cfg.schema.c_str(),
        cfg.port,
        nullptr,
        0);

    if (result == nullptr) {
        LOG_ERROR("database", "mysql_real_connect() failed: ", mysql_error(handle_));
        return false;
    }
    LOG_INFO("database", "Connected to MariaDB at ", cfg.host, ":", cfg.port);
    return true;
}

bool MariaDbClient::isConnected() const {
    return handle_ != nullptr;
}

// 发送 ping 命令到数据库，检查连接是否仍然活跃
// 返回 0 表示成功，非 0 表示失败（连接断开、超时等）
bool MariaDbClient::ping() {
    if (handle_ == nullptr) {
        return false;
    }
    return mysql_ping(handle_) == 0;
}

// 执行sql语句
bool MariaDbClient::execute(const std::string& query) {
    // 确保有连接
    if (handle_ == nullptr && !initialize()) {
        return false;
    }

    // 执行 SQL 查询
    if (mysql_query(handle_, query.c_str()) != 0) {
        // 查询失败 → 打印 SQL 错误
        LOG_ERROR("database", "Query failed: ", mysql_error(handle_), ". SQL: ", query);
        return false;
    }
    return true;
}

// 必须在 execute() 成功后立即调用
// 从服务器获取完整的结果集并存储在内存中
MYSQL_RES* MariaDbClient::storeResult() {
    if (handle_ == nullptr) {
        return nullptr;
    }
    return mysql_store_result(handle_);
}

// 关闭连接
void MariaDbClient::disconnect() {
    if (handle_ != nullptr) {
        mysql_close(handle_);
        handle_ = nullptr;
    }
}

} // namespace infrastructure::database
