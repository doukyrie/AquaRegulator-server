// 管理 TCP 客户端连接，接收命令，发送遥测数据给所有连接的客户端。
// 这个模块继承自 HPSocket 的 ServerListener，重写回调方法

#pragma once

#include <atomic>
#include <cstdint>
#include <cstring>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include <arpa/inet.h>

#include "core/configuration.hpp"
#include "core/logger.hpp"
#include "domain/telemetry_models.hpp"
#include "monitoring/health_monitor.hpp"
#include "transport/sensor_data_settings.hpp"
#include "network/server_listener_tcp.hpp"

class TelemetryPublisher : public ServerListener {
public:
    using SnapshotProvider = std::function<std::vector<domain::TelemetryFrame>()>;

    TelemetryPublisher(const core::PublisherConfig& config,
                       DeviceCommandRouter& router,
                       monitoring::HealthMonitor& monitor)
        : config_(config)
        , router_(router)
        , monitor_(monitor)
        , server_(this) {   // 把 this 传给 HPSocket，用于回调
    }

    // 启动 TCP 服务器
    bool start() 
    {
        //配置
        server_->SetMaxConnectionCount(config_.maxConnections); //最多连接数
        server_->SetWorkerThreadCount(config_.workerThreads);   //工作线程数

        //启动监听 （0.0.0.0:5555）
        if (!server_->Start(config_.bindAddress.c_str(), config_.port)) 
        {
            LOG_ERROR("telemetry_publisher", "Failed to start server on ", config_.bindAddress, ":", config_.port);
            return false;
        }
        monitor_.update("telemetry_publisher", true, "Server listening");
        LOG_INFO("telemetry_publisher", "Listening on ", config_.bindAddress, ":", config_.port);
        return true;
    }

    //停止监听
    void stop() 
    {
        server_->Stop();
        monitor_.update("telemetry_publisher", false, "Server stopped");
    }

    // 检查是否有客户端订阅（客户端连接）
    bool hasSubscribers() const 
    {
        return server_->GetConnectionCount() > 0;
    }

    // 发布遥测数据给所有连接的客户端
    void publish(const domain::TelemetryFrame& frame) 
    {
        // 优化：没有订阅者（客户端连接）就不发
        if (!hasSubscribers()) {
            return;
        }

        // 序列化 frame 为 JSON
        auto payload = domain::toJson(frame).dump();    //dump：json转成字符串（里面的参数，比如有时候会传4，表示缩进空格数，方便阅读）

        // 构造网络数据包：[4字节json长度][JSON内容]
        std::vector<uint8_t> buffer(sizeof(uint32_t) + payload.size());

        // 网络字节序（大端）：高字节在前
        uint32_t len = static_cast<uint32_t>(payload.size());
        uint32_t netLen = htonl(len);

        // 复制长度前缀到缓冲区前 4 个字节
        std::memcpy(buffer.data(), &netLen, sizeof(uint32_t));

        // 复制 JSON 数据到缓冲区后续位置
        std::memcpy(buffer.data() + sizeof(uint32_t), payload.data(), payload.size());

        //广播给所有连接的客户端（对每个客户端执行Send回调）
        forEachConnection([&](CONNID id) {
            server_->Send(id, buffer.data(), static_cast<int>(buffer.size()));
        });

        monitor_.update("telemetry_publisher", true, "Frame delivered to clients");
    }

    // 设置快照提供者（新客户端连接时发送历史快照）
    void setSnapshotProvider(SnapshotProvider provider) 
    {
        snapshotProvider_ = std::move(provider);
    }

protected:
    // HPSocket 回调：新客户端连接
    EnHandleResult OnAccept(ITcpServer* pSender, CONNID dwConnID, UINT_PTR soClient) override 
    {
        // 先调用父类的处理（把连接加入连接数组，更新最后连接id）
        auto result = ServerListener::OnAccept(pSender, dwConnID, soClient);
        monitor_.update("telemetry_publisher", true, "Client connected: " + std::to_string(dwConnID));

        // 新客户端连接上来时，发送历史快照
        if (snapshotProvider_) 
        {
            // 获取所有历史数据
            for (const auto& frame : snapshotProvider_()) 
            {
                publish(frame); //发送给所有连接的客户端
            }
        }
        return result;
    }

    // HPSocket 回调：客户端断开连接
    EnHandleResult OnClose(ITcpServer* pSender, CONNID dwConnID, EnSocketOperation op, int errorCode) override 
    {
        monitor_.update("telemetry_publisher", true, "Client disconnected: " + std::to_string(dwConnID));
        return ServerListener::OnClose(pSender, dwConnID, op, errorCode);
    }

    // HPSocket 回调：接收到数据
    EnHandleResult OnReceive(ITcpServer* pSender, CONNID dwConnID, const BYTE* pData, int iLength) override 
    {
        // 转换为字符串
        std::string chunk(reinterpret_cast<const char*>(pData), iLength);

        // 委托给命令路由器处理（解析包，写入寄存器）
        router_.feed(static_cast<uint64_t>(dwConnID), chunk, [&](const std::string& reply) {
            // 发送响应（添加 \n 作为分隔符）
            auto payload = reply + "\n";
            pSender->Send(dwConnID, reinterpret_cast<const BYTE*>(payload.data()), static_cast<int>(payload.size()));
        });
        return HR_OK;
    }

private:
    core::PublisherConfig config_;
    DeviceCommandRouter& router_;   // 获取客户端发来的包，解析后写入modbus寄存器
    monitoring::HealthMonitor& monitor_;    //健康检查
    SnapshotProvider snapshotProvider_; //std::function类型的回调函数
    CTcpServerPtr server_;  // HPSocket 服务器对象（构造时传入监听器）
};
