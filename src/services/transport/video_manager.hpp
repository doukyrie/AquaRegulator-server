#pragma once
#include <vector>
#include <mutex>
#include <thread>
#include <atomic>
#include <condition_variable>
#include <queue>
#include <memory>
#include <string>
#include <cstdint>
#include <unordered_map>

#include "HPSocket.h"  // 假设 HPSocket 已包含路径
#include "core/logger.hpp"
#include "monitoring/health_monitor.hpp"

// 视频数据包结构
struct VideoPacket {
    std::vector<uint8_t> data;  // 视频数据
    int64_t timestamp;  // 时间戳
};

// 视频客户端结构
struct VideoClient {
    CONNID id;  // 连接 ID
    bool isPublisher; // 是否是publisher
};

// 视频中转管理类
class VideoManager : public CTcpServerListener {
public:
    explicit VideoManager(monitoring::HealthMonitor* monitor = nullptr);
    ~VideoManager();

    // 启动视频服务器
    bool start(uint16_t port);

    // 停止视频服务器
    void stop();

    // 设置健康监控器
    void setHealthMonitor(monitoring::HealthMonitor* monitor) { healthMonitor_ = monitor; }

    // 推流客户端收到数据
    EnHandleResult OnReceive(ITcpServer* pSender, CONNID dwConnID, const BYTE* pData, int iLength) override;
    // 客户端连接/断开
    EnHandleResult OnAccept(ITcpServer* pSender, CONNID dwConnID, UINT_PTR soClient) override;
    EnHandleResult OnClose(ITcpServer* pSender, CONNID dwConnID, EnSocketOperation enOperation, int iErrorCode) override;

private:
    // 后台转发线程
    void relayThreadFunc();

private:
    CTcpServerPtr server_;  // 构造的时候传的是自己（this）
    std::thread relayThread_;   // 转发线程
    std::atomic<bool> running_{false};  // 运行标志

    std::mutex clientsMutex_;   // 保护 clients_
    std::unordered_map<CONNID, VideoClient> clients_;   // 连接到视频服务器的客户端（连接 ID → 客户端信息）

    std::mutex queueMutex_; // 保护数据包队列
    std::condition_variable queueCv_;   // 队列非空通知（条件变量）
    std::queue<VideoPacket> packetQueue_;   // 数据包队列（存视频数据包结构）
    monitoring::HealthMonitor* healthMonitor_{nullptr}; //健康监控
};
