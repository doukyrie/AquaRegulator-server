# AquaRegulator 服务端说明
## 主要更新


运行入口现在统一加载 JSON 配置，启动健康监控、遥测服务、TCP 数据发布器和视频转发，并支持 SIGINT/SIGTERM 优雅退出与配置重载钩子。


引入线程安全日志系统（可控级别、文件落盘），以及带默认模板的配置管理器。


新健康监控模块按固定周期输出各组件健康状态 JSON，便于外部探针读取。


传感器访问改为 SensorGateway，具备自动重连、读写异常告警和标准化数据封装。


构建完整遥测服务：带缓存的实时/历史采集、相关 ID 标记、客户端补播等企业级数据通道。


TCP 发布端配合指令路由器，支持诊断查询、配置热更新请求、安全寄存器写入和即时应答。


视频转发管理器区分推流/订阅角色并汇报健康状态。


CMake 目标新增核心/监控/数据库模块并链接线程库，契合新架构。

## 1. 依赖
- 编译：g++/clang++ (C++17)、cmake ≥ 3.16、make
- 库：MariaDB/MySQL 开发包、pthread（随系统）、nlohmann-json 头文件
- 第三方：仓库已内置 HPSocket 与 libmodbus 源码，CMake 会自动构建，无需单独安装 libmodbus
- Ubuntu/Debian 安装示例：
  ```bash
  sudo apt-get install build-essential cmake libmariadb-dev nlohmann-json3-dev
  ```

## 2. 配置
- 文件：`config/app_config.json`
- 项目：
  - `database`：host/user/password/schema/port
  - `sensor`：Modbus IP/port/registers/retrySeconds
  - `publisher`：遥测 TCP 监听地址/端口（默认 5555）、线程数、最大连接
  - `video`：视频端口（默认 6000）
  - `health`：健康文件路径与周期
  - `pipeline`：实时/历史采集周期、缓存大小
- 文件缺失会自动生成默认模板；按实际环境修改并保存。

## 3. 构建
```bash
mkdir -p build
cd build
cmake ..              # 自动构建 3rdparty/libmodbus 并使用 hp-socket
cmake --build . -j    # 生成 build/AquaRegS
```
- 构建后 `post_build.sh` 会创建 HPSocket 的 SO 链接；若运行时报缺库，设置：
  ```bash
  export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:/home/<user>/src/AquaRegulator/src
  ```

## 4. 运行
```bash
./build/AquaRegS   # 仓库根目录执行
```
- 日志：`logs/aqua_regulator.log`
- 健康：`artifacts/health_status.json`（路径由配置决定）
- 端口：遥测 `publisher.port`（默认 5555），视频 `video.port`（默认 6000）

## 5. 协议速览
- 遥测下行：TCP 帧 = 4 字节网络序长度前缀 + JSON。
  - `channel`: `realtime` / `historical_env` / `historical_soil`
  - `snapshot`: 首连快照为 true，实时帧为 false
  - `correlationId`, `readings[...]`：温湿光土气雨等数据
- 控制上行：JSON 文本，每条以 `\n` 结束；
  - 类型：`threshold` / `light_control` / `mode_select` / `write_register` / `diagnostics` / `config_reload`
  - 服务端返回一行 JSON ACK。
- 视频通道：
  - 推流端：连接后先发 `ROLE:PUBLISHER`（独立一条，可带换行），再推送视频数据。
  - 订阅端：可不发或发 `ROLE:SUBSCRIBER`；订阅端推送的视频会被忽略。

## 6. 验证
- 启动后检查日志确认数据库/Modbus/端口监听成功。
- 查看健康文件，`healthy` 为 true 且时间更新。
- 遥测端口：按长度前缀读取 JSON；向服务器发送 `{"type":"diagnostics"}\n` 应收到 ACK。
- 视频：推流端声明角色后推送，订阅端可收；若日志出现 “Subscriber attempted to push data”，说明角色声明未生效。

## 7. 部署提示
- 保持 `build/AquaRegS` 与 `config`、`3rdparty` 相对位置，便于库查找。
- 生产可用 systemd，工作目录设为仓库根，启动前导出 `LD_LIBRARY_PATH`。
- 防火墙放行配置的遥测/视频端口。
