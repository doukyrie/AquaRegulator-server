// 应用的启动入口，初始化所有模块，管理应用生命周期。

#include <atomic>
#include <chrono>
#include <csignal>
#include <thread>

#include "core/configuration.hpp"
#include "core/logger.hpp"
#include "services/data_manager.hpp"
#include "monitoring/health_monitor.hpp"
#include "infrastructure/sensors/sensor_data.hpp"
#include "transport/sensor_data_settings.hpp"
#include "transport/tcp_data_sender.hpp"
#include "services/transport/video_manager.hpp"
#include "infrastructure/database/telemetry_repository.hpp"

// 全局变量：用于信号处理
namespace {
std::atomic<bool>* g_shouldRun = nullptr;

// 信号处理函数（SIGINT 和 SIGTERM）
void handleSignal(int) 
{
    if (g_shouldRun) 
    {
        g_shouldRun->store(false);  // 设置退出标志
    }
}
}

int main() {
    // 设置最低日志级别、打印到哪个文件、是否要输出到控制台
    core::Logger::instance().configure(core::LogLevel::Info, "logs/aqua_regulator.log");

    //加载配置
    core::ConfigurationManager configManager("config/app_config.json");
    const auto& config = configManager.get();   //拿到配置，存在config里

    //创建健康监控，传入写健康状态的文件路径和检查间隔
    monitoring::HealthMonitor healthMonitor(config.health.statusFile,
                                            std::chrono::seconds(config.health.intervalSeconds));
    
    //后台专门有一个线程，定期将monitor中的states（各个模块的健康状态）写入文件中
    healthMonitor.start();

    //初始化数据库（数据通过TelemetryReading类型传输）
    infrastructure::database::TelemetryRepository repository;
    if (!repository.initialize(config.database)) {  // 初始化数据库配置
        LOG_CRITICAL("bootstrap", "Failed to connect to database. Exiting.");
        return EXIT_FAILURE;
    }

    // 初始化传感器网关（modbus读设备数据），传入健康监控
    SensorGateway sensorGateway(config.sensor, healthMonitor);
    std::atomic<bool> reloadRequested{false};   //重新加载请求

    
    TelemetryPublisher* publisherPtr = nullptr; // 保存指针用于诊断

    // 处理来自客户端的各种命令（JSON 格式），执行相应的动作（写入寄存器，更新健康检查），并返回响应
    DeviceCommandRouter router(
        sensorGateway,
        healthMonitor,
        [&]() { // 诊断信息提供者
            nlohmann::json json;
            json["telemetry"]["subscribers"] = publisherPtr ? publisherPtr->hasSubscribers() : false;   //返回是否有订阅者（客户端连接）
            json["pipeline"]["realtimeSeconds"] = config.pipeline.realtimeIntervalSeconds;  // 拿到modbus实时数据的采集间隔（5s）
            json["pipeline"]["historicalSeconds"] = config.pipeline.historicalIntervalSeconds;  // 拿到modbus历史数据的采集间隔（30s）
            return json;
        },
        [&]() { // 配置重载回调（将配置重新加载设置为true）
            reloadRequested.store(true);
        });

    // 启动遥测发布器
    // 管理 TCP 客户端连接，接收命令，发送遥测数据给所有连接的客户端
    TelemetryPublisher publisher(config.publisher, router, healthMonitor);
    publisherPtr = &publisher;  // 保存指针用于诊断

    //启动数据发布器
    if (!publisher.start()) {
        LOG_CRITICAL("bootstrap", "Failed to start telemetry publisher");
        return EXIT_FAILURE;
    }

    //启动遥测采集服务
    //定期从数据库、modbus获取数据，存入缓存，通过publisher发布给客户端
    TelemetryService telemetryService(config.pipeline, repository, sensorGateway, publisher, healthMonitor);
    telemetryService.start();

    // 启动视频管理器
    VideoManager videoManager(&healthMonitor);
    if (!videoManager.start(config.video.port)) {
        // 视频不是核心功能，失败了继续运行
        LOG_WARN("bootstrap", "Video manager failed to start");
    }

    // 注册信号处理器
    std::atomic<bool> shouldRun{true};
    g_shouldRun = &shouldRun;   //将全局变量绑定到这里

    std::signal(SIGINT, handleSignal);
    std::signal(SIGTERM, handleSignal);

    LOG_INFO("bootstrap", "AquaRegulator backend is running");

    // 当后台没接收到上面两个信号，则进入while循环运行
    while (shouldRun.load()) //读shouldRun的值
    {
        // 检查配置是否被修改
        if (reloadRequested.exchange(false))    //写入false并返回原来的值（如果原来的值为true则进入if，说明请求重新加载配置文件）
        {
            if (configManager.reloadIfChanged()) // 热加载：检查配置文件文件是否修改，如果修改就重新加载
            {
                LOG_INFO("bootstrap", "Configuration reload requested but runtime hot-reload not implemented for all services.");
            }
        } 
        else 
        {
            configManager.reloadIfChanged();
        }
        std::this_thread::sleep_for(std::chrono::seconds(5));
    }

    videoManager.stop();
    telemetryService.stop();
    publisher.stop();
    healthMonitor.stop();

    return EXIT_SUCCESS;
}
