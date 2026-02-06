// MariaDB Client（数据库连接管理）

#pragma once

#include <mariadb/mysql.h>
#include <string>

#include "core/configuration.hpp"

// mysql API基础概念
// MYSQL* handle
//   │
//   ├─ 代表一个数据库连接的生命周期
//   ├─ 通过 mysql_init() 创建
//   ├─ 通过 mysql_real_connect() 建立真实连接
//   ├─ 通过 mysql_query() 执行 SQL
//   ├─ 通过 mysql_store_result() 获取结果
//   └─ 通过 mysql_close() 关闭连接

// MYSQL_RES* 结果集
//   │
//   ├─ 代表 SELECT 查询的结果（多行多列）
//   ├─ 每一行是一个 MYSQL_ROW（即 char** 数组）
//   ├─ 通过 mysql_fetch_row() 逐行读取
//   └─ 通过 mysql_free_result() 释放内存


namespace infrastructure::database {

class MariaDbClient {
public:
    MariaDbClient();
    ~MariaDbClient();

    
    bool initialize();  // 初始化：创建 MySQL 上下文
    bool connect(const core::DatabaseConfig& cfg);  // 连接：建立到数据库的连接
    bool isConnected() const;   // 检查连接状态（主要是看handle是否被释放）
    bool ping();    // 检查连接是否仍然活跃

    bool execute(const std::string& query); // 执行任意 SQL 查询
    MYSQL_RES* storeResult();   // 获取查询结果（仅在 execute 成功后调用）

    void disconnect();  // 断开连接

private:
    MYSQL* handle_{nullptr};    // MySQL 连接句柄
    core::DatabaseConfig config_;   // 保存配置用于重连
};

} // namespace infrastructure::database
