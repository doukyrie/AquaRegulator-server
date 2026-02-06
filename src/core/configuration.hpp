#pragma once

#include <chrono>
#include <filesystem>
#include <optional>
#include <string>

namespace core {

// 数据库配置
struct DatabaseConfig {
    std::string host = "127.0.0.1";
    std::string user = "root";
    std::string password = "password";
    std::string schema = "testdb";  // 数据库名称
    uint16_t port = 3306;
    uint16_t readRecentLimit = 50;  // 查询最多返回 50 条
    uint16_t retrySeconds = 5;  // 重连间隔 5 秒
};

// Modbus 传感器配置
struct SensorConfig {
    std::string endpoint = "127.0.0.1"; //ip地址
    uint16_t port = 502;    //端口
    uint16_t retrySeconds = 5;  //重连间隔
    uint16_t registers = 6; //寄存器数量
};

// TCP 发布器配置（网络服务器）
struct PublisherConfig {
    std::string bindAddress = "0.0.0.0";
    uint16_t port = 5555;
    uint16_t workerThreads = 4; //线程数
    uint16_t maxConnections = 200;  //最大连接数
};

// 视频模块配置
struct VideoConfig {
    uint16_t port = 6000;
};

// 健康监控配置
struct HealthConfig {
    std::string statusFile = "artifacts/health_status.json";
    uint16_t intervalSeconds = 5;
};

// 数据采集管道配置（modbus传感器）
struct PipelineConfig {
    uint16_t realtimeIntervalSeconds = 5;   // 实时数据每 5 秒采集一次
    uint16_t historicalIntervalSeconds = 30;    // 历史数据每 30 秒采集一次
    uint16_t cacheSize = 120;   //// 缓存 120 条数据
};

// 聚合所有配置
struct Configuration {
    DatabaseConfig database;
    SensorConfig sensor;
    PublisherConfig publisher;
    VideoConfig video;
    HealthConfig health;
    PipelineConfig pipeline;
};

// 配置管理器
class ConfigurationManager {
public:
    // 构造函数：加载配置文件（app_config.json）
    // explicit防止隐式转换，即要使用对象必须显式调用构造函数（构造函数的隐式转换发生在只有一个参数的构造函数）
    // 假设有个函数void printConfig(ConfigurationManager config)，如果传入printConfig("C:/settings.json"); 
    // 编译器会隐式创建一个临时ConfigurationManager对象，用"C:/settings.json"构造，加了explict就不允许这种写法，必须传入一个构造好的对象
    explicit ConfigurationManager(std::string path);

    // 获取配置（const 引用，保证不被修改）
    const Configuration& get() const { return config_; }

    // 检查文件是否修改，如果修改就重新加载配置文件
    bool reloadIfChanged();

private:
    void loadFromDisk();    // 从磁盘加载
    static Configuration fromJson(const std::string& jsonText); // 从 JSON 字符串解析配置并返回聚合配置
    static std::string defaultJson();   // 生成默认配置 JSON（文件不存在时）

    Configuration config_;  //聚合配置（所有配置的对象集合）
    std::string path_;  //配置文件路径
    std::filesystem::file_time_type lastWriteTime_{};   //最后一次修改文件的时间
};

} // namespace core
