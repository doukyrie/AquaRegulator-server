#pragma once

#include <atomic>
#include <chrono>
#include <map>
#include <mutex>
#include <string>
#include <thread>

namespace monitoring {

// 单个组件的健康状态
struct HealthState {
    bool healthy{false};    // 是否健康
    std::string detail; // 详细信息
    std::chrono::system_clock::time_point updatedAt;    // 更新时间
};

class HealthMonitor {
public:
    // 构造函数
    // path: 健康状态输出文件路径
    // interval: 检查间隔
    HealthMonitor(std::string path, std::chrono::seconds interval);
    ~HealthMonitor();

    // 启动监控线程
    void start();

    // 停止监控线程
    void stop();

    // 更新某个组件的健康状态
    void update(const std::string& component, bool healthy, const std::string& detail);

private:
    // 后台工作线程主函数
    void writerLoop();

    // 把当前状态写入文件
    void flushToDisk();

    std::string filePath_;  //写入健康检查的文件路径
    std::chrono::seconds interval_; // 检查间隔

    std::map<std::string, HealthState> states_; //存储健康检查信息
    std::mutex mutex_;

    std::thread worker_;    
    std::atomic<bool> running_{false};  //线程是否循环
};

} // namespace monitoring
