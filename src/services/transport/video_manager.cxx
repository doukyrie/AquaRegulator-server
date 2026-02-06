#include "video_manager.hpp"

#include <iostream>
#include <chrono>

VideoManager::VideoManager(monitoring::HealthMonitor* monitor) 
    : server_(this)
    , healthMonitor_(monitor)
{
}

VideoManager::~VideoManager() {
    stop();
}


bool VideoManager::start(uint16_t port) {
    // 启动 HPSocket 服务器
    if (!server_->Start(nullptr, port)) // 传的是视频模块配置中的端口
    {
        LOG_ERROR("video_manager", "Failed to start server on port ", port);

        //更新健康检查
        if (healthMonitor_) 
        {
            healthMonitor_->update("video_manager", false, "Start failed");
        }
        return false;
    }

    // 启动转发线程
    running_ = true;
    relayThread_ = std::thread(&VideoManager::relayThreadFunc, this);
    LOG_INFO("video_manager", "Started on port ", port);
    if (healthMonitor_) 
    {
        healthMonitor_->update("video_manager", true, "Listening on port " + std::to_string(port));
    }
    return true;
}

void VideoManager::stop() {
    if (!running_) return;
    running_ = false;
    queueCv_.notify_all();  // 唤醒转发线程
    if (relayThread_.joinable()) relayThread_.join();
    server_->Stop();
}

EnHandleResult VideoManager::OnAccept(ITcpServer* pSender, CONNID dwConnID, UINT_PTR soClient) {
    // 新客户端连接
    std::lock_guard<std::mutex> lk(clientsMutex_);

    VideoClient client{dwConnID, false};    // 默认为非publisher
    clients_[dwConnID] = client;

    LOG_INFO("video_manager", "Client connected: ", dwConnID);
    if (healthMonitor_) {
        healthMonitor_->update("video_manager", true, "Client connected: " + std::to_string(dwConnID));
    }
    return HR_OK;
}

EnHandleResult VideoManager::OnClose(ITcpServer* pSender, CONNID dwConnID, EnSocketOperation, int) {
    std::lock_guard<std::mutex> lk(clientsMutex_);

    clients_.erase(dwConnID);   //删掉视频客户端的连接

    LOG_INFO("video_manager", "Client disconnected: ", dwConnID);
    if (healthMonitor_) 
    {
        healthMonitor_->update("video_manager", true, "Client disconnected: " + std::to_string(dwConnID));
    }

    return HR_OK;
}

//pData：数据   iLength：数据长度
EnHandleResult VideoManager::OnReceive(ITcpServer* pSender, CONNID dwConnID, const BYTE* pData, int iLength) {
    if (iLength <= 0) return HR_OK;

    //将数据构造成字符串
    std::string payload(reinterpret_cast<const char*>(pData), iLength);

    // 检查是否是角色声明（格式：ROLE:PUBLISHER 或 ROLE:SUBSCRIBER）
    // 处理角色声明（如果是PUBLISHER则isPublisher存true）
    if (payload.rfind("ROLE:", 0) == 0) {   //检查前缀是否是ROLE:
        std::string role = payload.substr(5);   // 从指定的 start_index（5，也就是"ROLE:"之后） 开始，一直截取到字符串的末尾

        std::lock_guard<std::mutex> lk(clientsMutex_);
        auto it = clients_.find(dwConnID);
        if (it != clients_.end()) 
        {
            it->second.isPublisher = (role == "PUBLISHER"); //若为PUBLISHER则存true，否则存false
            LOG_INFO("video_manager", "Client ", dwConnID, " role updated -> ", role);
        }
        return HR_OK;
    }

    // 检查该连接是否是推流端
    {
        std::lock_guard<std::mutex> lk(clientsMutex_);
        //视频客户端连接中有dwConnID，但不是推送端，则不推送数据，直接返回
        if (auto it = clients_.find(dwConnID); it != clients_.end() && !it->second.isPublisher) 
        {
            LOG_WARN("video_manager", "Subscriber ", dwConnID, " attempted to push data. Ignored.");
            return HR_OK;
        }
    }

    // 这是推流端发来的视频数据 → 入队（将视频数据存入视频数据结构中的data）
    VideoPacket pkt;
    pkt.data.assign(pData, pData + iLength);
    pkt.timestamp = std::chrono::steady_clock::now().time_since_epoch().count();
    //入队
    {
        std::lock_guard<std::mutex> lk(queueMutex_);
        packetQueue_.push(pkt);
    }

    //唤醒转发线程
    queueCv_.notify_one();

    return HR_OK;
}

void VideoManager::relayThreadFunc() {
    // 转发线程主循环
    while (running_) {
        std::unique_lock<std::mutex> lk(queueMutex_);

        // 等待队列非空或收到停止信号
        queueCv_.wait(lk, [this]() { return !packetQueue_.empty() || !running_; }); //后面的lambda表达式是唤醒条件
        if (!running_) break;

        // 从队列取出一个包
        VideoPacket pkt = std::move(packetQueue_.front());  //先移动
        packetQueue_.pop(); //再出队
        lk.unlock();    // 提前解锁，避免持有锁时发送数据

        // 广播给所有订阅客户端
        std::lock_guard<std::mutex> lk2(clientsMutex_);
        for (auto& [id, client] : clients_) {
            // 只发给订阅端（isPublisher = false）
            if (!client.isPublisher) {  //如果不是推流端（说明是订阅端）
                server_->Send(id, pkt.data.data(), static_cast<int>(pkt.data.size()));
            }
        }

        if (healthMonitor_) {
            healthMonitor_->update("video_manager", true, "Video packet broadcast");
        }
    }
}
