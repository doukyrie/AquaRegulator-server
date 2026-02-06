#pragma once

#include "HPSocket.h"

#include <algorithm>
#include <functional>
#include <iostream>
#include <mutex>

//继承 HPSocket 的监听器基类
class ServerListener : public CTcpServerListener
{
private:
    CONNID _connID = 0; // 记录最后一个连接的 ID（注意：并发时会被覆盖）
    std::vector<CONNID> _connIDs;   //所有活跃连接（只要建立连接没有断开都算活跃连接）
    mutable std::mutex _connMutex;  //线程锁

public:
    // 当服务器准备开始监听某个端口时触发
    // pSender: 指向当前的服务器组件对象
    // soListen: 底层的监听 SOCKET 句柄
    EnHandleResult OnPrepareListen(ITcpServer *pSender, SOCKET soListen) override
    {
        // std::cout << "[Server] Ready to listen..." << std::endl;
        return HR_OK;   //允许启动
    }

    //当有新客户端连接时触发（把连接加入连接数组，更新最后连接id）
    EnHandleResult OnAccept(ITcpServer *pSender, CONNID dwConnID, UINT_PTR soClient) override
    {
        _connID = dwConnID; //更新最后连接的id
        {
            // 加锁：防止在记录新连接时，另一个线程正在进行断开清理或遍历
            std::lock_guard<std::mutex> lk(_connMutex);

            // 将新连接的唯一标识存入数组
            _connIDs.push_back(dwConnID);
        }

        std::cout << "[Server] Client connected: " << dwConnID << std::endl;
        return HR_OK;   // 确认接受此连接
    }

    //当该连接有数据到达时触发
    //    pData = 接收到的字节
    //    iLength = 字节数
    EnHandleResult OnReceive(ITcpServer *pSender, CONNID dwConnID, const BYTE *pData, int iLength) override
    {
        std::string msg((const char *)pData, iLength);  // 将原始字节流转换为 std::string 方便打印
        std::cout << "[Server] Received: " << msg << std::endl;

        std::string reply = "Server reply: " + msg; //构造回复字符串（回声）
        pSender->Send(dwConnID, (const BYTE *)reply.c_str(), reply.size()); // 调用组件方法，通过 ID 将数据发回给特定的客户端
        return HR_OK;
    }

    // 当连接关闭时触发（客户端断开或错误）
    EnHandleResult OnClose(ITcpServer *pSender, CONNID dwConnID, EnSocketOperation, int iErrorCode) override
    {
        std::cout << "[Server] Client disconnected: " << dwConnID << std::endl;
        std::lock_guard<std::mutex> lk(_connMutex); // 再次加锁保护数组

        // 使用 Erase-Remove 惯用法：从数组中找到并删除对应的 dwConnID
        _connIDs.erase(
            std::remove(_connIDs.begin(), _connIDs.end(), dwConnID),
            _connIDs.end());
        return HR_OK;
    }


    CONNID 
    getConnectionID() const 
    { return _connID; }

    std::vector<CONNID> 
    getAllConnectionIDs() const 
    { 
        std::lock_guard<std::mutex> lk(_connMutex);
        return _connIDs; 
    }

    // 判断当前是否有客户端在线
    bool hasConnections() const
    {
        std::lock_guard<std::mutex> lk(_connMutex);
        return !_connIDs.empty();
    }

    //遍历所有连接
    template <typename Fn>
    void forEachConnection(Fn&& fn) const   //传入回调函数（万能引用）
    {
        // 先拷贝一份列表，这样在遍历执行 fn 时不需要持有锁，提高并发效率
        auto ids = getAllConnectionIDs();

        for (const auto id : ids) {
            fn(id); // 执行传入的函数或 lambda
        }
    }
};
