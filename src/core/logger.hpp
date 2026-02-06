#pragma once

#include <atomic>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <string>
#include <string_view>

namespace core {

enum class LogLevel {
    Trace = 0,  //指定枚举从0开始，后面的都等于前项+1
    Debug,
    Info,
    Warn,
    Error,
    Critical
};


// Logger 类（单例模式）
class Logger {
public:
    // 获取全局唯一的 Logger 实例
    static Logger& instance();

    // 配置日志（启动时调用一次）
    // level: 最低日志级别
    // filePath: 日志文件路径
    // useConsole: 是否同时输出到控制台
    void configure(LogLevel level, const std::string& filePath = "", bool useConsole = true);

    // 可变参数模板日志函数
    // 支持任意数量的参数，比如：
    // LOG_INFO("component", "msg1", 100, "msg3");
    template <typename... Args>
    void log(LogLevel level, std::string_view component, Args&&... args) {
        // 检查日志级别（优化：不符合级别就直接返回）
        if (level < minLevel_.load()) {
            return;
        }

        // 用 ostringstream 收集所有参数
        std::ostringstream oss;

        // 折叠表达式（C++17）
        // 展开为：oss << arg1 << arg2 << arg3 << ... << argN
        (oss << ... << std::forward<Args>(args));

        // 委托给 write() 实际写入
        write(level, component, oss.str());
    }

private:
    Logger() = default;
    ~Logger() = default;

    // 实际的日志写入逻辑
    void write(LogLevel level, std::string_view component, const std::string& message);

    // 将日志级别（枚举）转换为字符串
    std::string levelToString(LogLevel level) const;

    // 成员变量
    std::mutex mutex_;  // 互斥锁（线程安全）

    // atomic：对 minLevel_ 这个变量的读写操作必须是原子性的
    // 大括号 {} 是 C++11 引入的列表初始化语法
    std::atomic<LogLevel> minLevel_{LogLevel::Info};    // 最低日志级别
    std::ofstream fileStream_;  // 文件流
    bool consoleEnabled_{true}; // 是否输出到控制台
    bool fileEnabled_{false};   // 是否输出到文件
};

} // namespace core

//__VA_ARGS__会原封不动地将...中的参数替换过来
#define LOG_TRACE(component, ...) ::core::Logger::instance().log(::core::LogLevel::Trace, component, __VA_ARGS__)
#define LOG_DEBUG(component, ...) ::core::Logger::instance().log(::core::LogLevel::Debug, component, __VA_ARGS__)
#define LOG_INFO(component, ...)  ::core::Logger::instance().log(::core::LogLevel::Info, component, __VA_ARGS__)
#define LOG_WARN(component, ...)  ::core::Logger::instance().log(::core::LogLevel::Warn, component, __VA_ARGS__)
#define LOG_ERROR(component, ...) ::core::Logger::instance().log(::core::LogLevel::Error, component, __VA_ARGS__)
#define LOG_CRITICAL(component, ...) ::core::Logger::instance().log(::core::LogLevel::Critical, component, __VA_ARGS__)
