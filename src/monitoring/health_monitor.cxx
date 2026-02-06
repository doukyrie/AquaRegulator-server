// 定期收集各个模块的健康状态，写入 JSON 文件（本地文件），供外部监控系统（如 Prometheus、Kubernetes）读取

#include "monitoring/health_monitor.hpp"

#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>

#include "core/logger.hpp"

namespace monitoring {

HealthMonitor::HealthMonitor(std::string path, std::chrono::seconds interval)
    : filePath_(std::move(path))
    , interval_(interval) {
}

HealthMonitor::~HealthMonitor() {
    stop();
}

void HealthMonitor::start() {
    // 检查是否已经启动
    if (running_.exchange(true)) {
        return;
    }

    // 启动后台工作线程
    // 非静态成员函数其实都有一个隐藏的第一个参数，那就是 this 指针
    // 当你调用 HealthMonitor::writerLoop 时，它是一个成员函数。成员函数必须绑定到一个具体的对象实例上才能运行
    worker_ = std::thread(&HealthMonitor::writerLoop, this);
}

void HealthMonitor::stop() {
    if (!running_.exchange(false)) {
        return;
    }
    if (worker_.joinable()) {
        worker_.join(); // 等待工作线程完成
    }
}

// 更新健康状态
void HealthMonitor::update(const std::string& component, bool healthy, const std::string& detail) {
    std::lock_guard<std::mutex> lk(mutex_);
    states_[component] = HealthState{healthy, detail, std::chrono::system_clock::now()};
}


void HealthMonitor::writerLoop() {
    // 后台线程主循环
    while (running_) {
        // 把状态写入文件
        flushToDisk();

        // 睡眠指定的间隔
        std::this_thread::sleep_for(interval_);
    }

    // 线程退出前最后写一次
    flushToDisk();
}

void HealthMonitor::flushToDisk() {
    // 获取当前状态的快照
    std::map<std::string, HealthState> snapshot;
    {
        std::lock_guard<std::mutex> lk(mutex_);
        snapshot = states_;
    }// 锁自动释放

    // 转换为 JSON
    nlohmann::json json;
    for (const auto& [component, state] : snapshot) {
        // 把时间点转换为 Unix 时间戳
        auto time = std::chrono::system_clock::to_time_t(state.updatedAt);
        json[component] = {
            {"healthy", state.healthy},
            {"detail", state.detail},
            {"updatedAt", time}
        };
    }

    try {
        // 确保目录存在
        std::filesystem::create_directories(std::filesystem::path(filePath_).parent_path());

        // 写入文件（覆盖模式）
        std::ofstream out(filePath_, std::ios::out | std::ios::trunc);
        out << json.dump(4);    // 缩进 4 个空格
    } catch (const std::exception& ex) {
        LOG_ERROR("health_monitor", "Failed to persist health information: ", ex.what());
    }
}

} // namespace monitoring
