#include "core/logger.hpp"

#include <ctime>
#include <filesystem>
#include <iostream>

namespace core {

//单例
Logger& Logger::instance() 
{
    // C++11 保证线程安全的静态初始化
    //从 C++11 开始，标准规定了：如果多个线程试图同时初始化同一个静态局部变量，初始化行为会序列化发生。 
    //也就是说，编译器会自动帮你加锁，确保只有一个线程执行构造，其他线程会阻塞等待直到构造完成
    static Logger instance; // static 变量在第一次调用instance时初始化，之后返回同一个实例
    return instance;
}

//配置方法（设置最低日志级别、打印到哪个文件、是否要输出到控制台）
void Logger::configure(LogLevel level, const std::string& filePath, bool useConsole) 
{
    // 设置最低级别（原子操作）
    minLevel_.store(level);
    consoleEnabled_ = useConsole;   //设置是否输出到控制台

    // 如果指定了文件路径（传入的path不为空），打开文件
    if (!filePath.empty()) 
    {
        // 创建多级目录（指的是如果路径为data/logs/v1这样三级目录,但是此时只有data一级，则调用创建多级目录创建这个三级的）（如果存在就返回false，在这里不影响）
        std::filesystem::create_directories(std::filesystem::path(filePath).parent_path()); 
        fileStream_.open(filePath, std::ios::out | std::ios::app);  //打开模式：输出（写入文件）、append（每次写入追加到文件末尾）
        fileEnabled_ = fileStream_.is_open();   //文件如果打开，则是否输出到文件置为true
    }
}

// 日志级别转字符串
std::string Logger::levelToString(LogLevel level) const 
{
    switch (level) 
    {
    case LogLevel::Trace: return "TRACE";
    case LogLevel::Debug: return "DEBUG";
    case LogLevel::Info: return "INFO";
    case LogLevel::Warn: return "WARN";
    case LogLevel::Error: return "ERROR";
    case LogLevel::Critical: return "CRITICAL";
    default: return "UNKNOWN";
    }
}

// 核心日志写入逻辑
//level：写入的日志级别
//component：代码模块
//message：写入的内容
void Logger::write(LogLevel level, std::string_view component, const std::string& message) 
{
    std::lock_guard<std::mutex> lk(mutex_); //加锁

    // 获取当前时间
    auto now = std::chrono::system_clock::now();    //获取当前时间点（此时的时间是以“时钟周期”衡量的，人类无法直接阅读）

    //将 chrono 时间点转换为传统的 std::time_t（c风格的时间戳）
    //time_t: 通常是一个长整型（long），表示从 Unix 纪元（1970年1月1日 00:00:00 UTC）到现在的秒数
    std::time_t time = std::chrono::system_clock::to_time_t(now);   

    // C 风格的结构体，包含年、月、日、时、分、秒等成员
    std::tm tm{};   //初始化为空

    // 线程安全地获取本地时间
#ifdef _WIN32
    localtime_s(&tm, &time);    //windows
#else
    localtime_r(&time, &tm);    //linux
#endif

    // 格式化前缀：[2024-01-14 10:30:45] [INFO] [sensor_gateway]
    std::ostringstream prefix;
    prefix << std::put_time(&tm, "%Y-%m-%d %H:%M:%S")
           << " [" << levelToString(level) << "]"
           << " [" << component << "] ";


    if (consoleEnabled_) //输出到控制台
    {  
        std::cout << prefix.str() << message << std::endl;
    }
    if (fileEnabled_) //输出到文件
    { 
        fileStream_ << prefix.str() << message << std::endl;
    }
}

} // namespace core
