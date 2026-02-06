// 处理来自客户端的各种命令（解析JSON），执行相应的动作（写入寄存器，更新健康检查），并返回响应

#pragma once

#include <cstdint>
#include <functional>
#include <iostream>
#include <optional>
#include <string>
#include <unordered_map>

#include <nlohmann/json.hpp>

#include "infrastructure/sensors/sensor_data.hpp"

class DeviceCommandRouter 
{
public:
    // 函数类型定义（使用别名简化代码）
    using DiagnosticProvider = std::function<nlohmann::json()>;
    using ReloadCallback = std::function<void()>;
    using ResponseCallback = std::function<void(const std::string&)>;

    // 构造函数
    DeviceCommandRouter(SensorGateway& gateway, // 传感器网关
                        monitoring::HealthMonitor& monitor, // 健康监控
                        DiagnosticProvider diagnostics, // 诊断信息提供者
                        ReloadCallback reloadCallback)  // 配置重载回调
        : sensorGateway_(gateway)
        , monitor_(monitor)
        , diagnosticsProvider_(std::move(diagnostics))
        , reloadCallback_(std::move(reloadCallback)) {}

    // 处理从客户端接收到的数据块（可能不是完整命令）
    // 这个函数会：
    // 1. 把数据块追加到缓冲区
    // 2. 按 \n 分割命令
    // 3. 对每条完整命令调用 parseLine()
    // 4. 通过 respond 回调发送响应
    void feed(uint64_t connectionId, const std::string& chunk, const ResponseCallback& respond) 
    {
        //分包处理
        auto& buffer = buffers_[connectionId];  // 获取该连接的缓冲区
        buffer += chunk;    // 追加新数据

        // 按 \n 分割命令
        size_t pos;
        
        while ((pos = buffer.find('\n')) != std::string::npos)
        {    //只要能在 buffer 中找到换行符，就继续循环（npos表示未找到）
            // 提取一行（不包括 \n），提取子串，从位置0提取pos个字符
            std::string line = buffer.substr(0, pos);

            // 删除已处理的部分（包括 \n）
            buffer.erase(0, pos + 1);

            // 解析命令并执行
            auto reply = parseLine(line);

            // 如果有响应，通过回调发送出去
            if (!reply.empty() && respond) 
            {
                respond(reply); //写modbus寄存器 + 返回响应（200）
            }
        }
    }

private:
    // 解析单行命令（假设该行是 JSON）
    std::string parseLine(const std::string& line) {
        try {
            auto msg = nlohmann::json::parse(line);
            const std::string type = msg.value("type", ""); //从 msg 对象中查找键名为 "type" 的值，若不存在则返回第二个参数（此处为空）

            // 根据类型用不同的处理函数进行处理，并返回响应内容
            if (type == "threshold") 
            {
                handleThreshold(msg);
                return R"({"status":"ok","message":"threshold updated"})";
            } 
            else if (type == "light_control") 
            {
                handleLightControl(msg);
                return R"({"status":"ok","message":"light control updated"})";
            } 
            else if (type == "mode_select") 
            {
                handleModeSelect(msg);
                return R"({"status":"ok","message":"mode updated"})";
            } 
            else if (type == "diagnostics") 
            {
                // 获取诊断信息（是否有客户端连接、实时数据和历史数据的采集间隔）
                return diagnosticsProvider_().dump();
            } 
            else if (type == "config_reload") 
            {
                if (reloadCallback_) 
                {
                    reloadCallback_();  //配置重载回调（成员属性）
                }
                return R"({"status":"ok","message":"configuration reload requested"})";
            } 
            else if (type == "write_register") 
            {
                handleDirectWrite(msg);
                return R"({"status":"ok","message":"register write queued"})";
            }
            return R"({"status":"error","message":"unknown command"})";
        } 
        catch (const std::exception& ex) 
        {
            monitor_.update("command_router", false, ex.what());
            return R"({"status":"error","message":"invalid payload"})";
        }
    }

    // 处理 threshold 命令
    // 命令格式: {"type":"threshold","soil":50.0,"rain":30.0,"temp":25.0,"light":800}
    void handleThreshold(const nlohmann::json& msg) {
        double soil = msg.value("soil", 0.0);
        double rain = msg.value("rain", 0.0);
        double temp = msg.value("temp", 0.0);
        double light = msg.value("light", 0.0);

        // 把浮点数转换为整数（*100），写入寄存器
        sensorGateway_.writeRegister(10, static_cast<uint16_t>(soil * 100));
        sensorGateway_.writeRegister(11, static_cast<uint16_t>(rain * 100));
        sensorGateway_.writeRegister(12, static_cast<uint16_t>(temp * 100));
        sensorGateway_.writeRegister(13, static_cast<uint16_t>(light * 100));
        monitor_.update("command_router", true, "threshold updated");
    }

    // 处理 light_control 命令
    // 命令格式: {"type":"light_control","light":100}
    void handleLightControl(const nlohmann::json& msg) {
        double light = msg.value("light", 0.0);
        sensorGateway_.writeRegister(14, static_cast<uint16_t>(light * 100));
        monitor_.update("command_router", true, "light control updated");
    }

    // 处理 mode_select 命令
    // 命令格式: {"type":"mode_select","mode":1}
    void handleModeSelect(const nlohmann::json& msg) {
        int mode = msg.value("mode", 0);
        sensorGateway_.writeRegister(15, static_cast<uint16_t>(mode));
        monitor_.update("command_router", true, "mode updated");
    }

    // 处理 write_register 命令
    // 命令格式: {"type":"write_register","address":20,"value":5000}
    void handleDirectWrite(const nlohmann::json& msg) {
        int address = msg.value("address", -1);
        int value = msg.value("value", 0);
        if (address >= 0) {
            sensorGateway_.writeRegister(static_cast<uint16_t>(address), static_cast<uint16_t>(value));
        }
    }

    SensorGateway& sensorGateway_;  // 传感器网关引用（连接modbus，读实时数据、写数据等）
    monitoring::HealthMonitor& monitor_;    // 健康监控引用
    DiagnosticProvider diagnosticsProvider_;    // 诊断信息回调
    ReloadCallback reloadCallback_; // 配置重载回调
    std::unordered_map<uint64_t, std::string> buffers_; // TCP 粘包处理：为每个连接维护一个缓冲区
};

// 命令格式
// // 1. 设置阈值
// {"type":"threshold","soil":50.0,"rain":30.0,"temp":25.0,"light":800}
// Response: {"status":"ok","message":"threshold updated"}

// // 2. 灯光控制
// {"type":"light_control","light":100}
// Response: {"status":"ok","message":"light control updated"}

// // 3. 模式选择
// {"type":"mode_select","mode":1}
// Response: {"status":"ok","message":"mode updated"}

// // 4. 诊断查询
// {"type":"diagnostics"}
// Response: {
//   "telemetry": {"subscribers": true},
//   "pipeline": {"realtimeSeconds": 5, "historicalSeconds": 60}
// }

// // 5. 配置重载
// {"type":"config_reload"}
// Response: {"status":"ok","message":"configuration reload requested"}

// // 6. 直接写寄存器
// {"type":"write_register","address":20,"value":5000}
// Response: {"status":"ok","message":"register write queued"}



// 粘包分包问题
// 问题场景：
  
//   客户端发送命令：
//     {"type":"threshold","soil":50}
//     {"type":"diagnostics"}
  
//   网络可能会：
//     1. 分成多个包：
//        数据包1: {"type":"threshold","so
//        数据包2: il":50}\n{"type":"diag
//        数据包3: nostics"}\n
       
//     2. 或者合并成一个包：
//        数据包1: {"type":"threshold"...}\n{"type":"diagnostics"}\n
       
//     3. 或者在奇怪的位置分割：
//        数据包1: {"type"...
//        （完全没有 \n）

// 解决方案：
  
//   OnReceive() 被调用 3 次
//   ├─ 调用 1: feed(connId, "{\"type\":\"threshold\",\"so")
//   │          buffers_[connId] = "{\"type\":\"threshold\",\"so"
//   │          没有 \n，不解析
//   │
//   ├─ 调用 2: feed(connId, "il\":50}\\n{\"type\":\"diag")
//   │          buffers_[connId] = "{\"type\":\"threshold\"...}\\n{\"type\":\"diag"
//   │          找到 \n，分割并解析第一个命令
//   │          buffers_[connId] = "{\"type\":\"diag"
//   │
//   └─ 调用 3: feed(connId, "nostics\"}\\n")
//              buffers_[connId] = "{\"type\":\"diagnostics\"}\n"
//              找到 \n，分割并解析第二个命令
//              buffers_[connId] = ""（空）