#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace domain {

// 遥测通道枚举（读到的3种类型的数据，实时、历史环境、历史土壤）
enum class TelemetryChannel 
{
    Realtime,
    HistoricalEnvironment,
    HistoricalSoil
};

// 为 TelemetryChannel 提供 Hash 函数
// 用途：可以把 TelemetryChannel 用作 unordered_map 的 key
struct TelemetryChannelHash 
{
    std::size_t operator()(TelemetryChannel channel) const noexcept 
    {
        return static_cast<std::size_t>(channel);   // 把枚举转为 size_t，用于计算 hash 值
    }
};

// 单次读数数据（只存一行的数据）
struct TelemetryReading 
{
    std::string label{"Realtime"};  // "Realtime", "Historical_ENV" 等
    std::string timestamp;  // "2024-01-14 10:30:45"
    double temperature{0.0};    // 温度（°C）
    double humidity{0.0};   // 湿度（%）
    double light{0.0};  // 光照（lux）
    double soil{0.0};   // 土壤含水量（%）
    double gas{0.0};    // 气体浓度（ppm）
    double raindrop{0.0};   // 雨量（mm） 
};

// 一次遥测传输的数据包
struct TelemetryFrame 
{
    TelemetryChannel channel{TelemetryChannel::Realtime};   //数据类型
    std::vector<TelemetryReading> readings; // 数据（类型为TelemetryReading，可能包含多次读数）
    bool snapshot{true};    // true=首次连接的快照, false=增量数据
    std::string correlationId;  // 用于追踪关联的请求
};

// 通道名称转换（用于 JSON 序列化）
inline std::string channelName(TelemetryChannel channel) 
{
    switch (channel) 
    {
    case TelemetryChannel::Realtime: return "realtime";
    case TelemetryChannel::HistoricalEnvironment: return "historical_env";
    case TelemetryChannel::HistoricalSoil: return "historical_soil";
    default: return "unknown";
    }
}

// TelemetryReading → JSON 序列化
inline nlohmann::json toJson(const TelemetryReading& reading) 
{
    return nlohmann::json
    {
        {"label", reading.label},
        {"timestamp", reading.timestamp},
        {"temperature", reading.temperature},
        {"humidity", reading.humidity},
        {"light", reading.light},
        {"soil", reading.soil},
        {"gas", reading.gas},
        {"raindrop", reading.raindrop}
    };
}

// TelemetryFrame → JSON 序列化
inline nlohmann::json toJson(const TelemetryFrame& frame) 
{
    nlohmann::json json;
    json["channel"] = channelName(frame.channel);
    json["snapshot"] = frame.snapshot;
    json["correlationId"] = frame.correlationId;
    json["readings"] = nlohmann::json::array(); //初始化一个array

    //将readings容器中的数据加入array中
    for (const auto& reading : frame.readings) 
    {
        json["readings"].push_back(toJson(reading));
    }
    return json;
}

} // namespace domain
