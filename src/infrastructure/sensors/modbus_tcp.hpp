// 通过 Modbus TCP 协议与传感器通讯，读写寄存器

#pragma once

#include <iostream>
#include <vector>

#include "modbus.h"

// 传感器数据映射例子：
// 每个寄存器 2 个字节
//   寄存器 0: 土壤含水量 * 100     (4500 = 45.00%)
//   寄存器 1: 气体浓度 * 100       (1050 = 10.50 ppm)
//   寄存器 2: 雨量 * 100           (0 = 0 mm)
//   寄存器 3: 温度 * 100           (2550 = 25.50°C)
//   寄存器 4: 湿度 * 100           (6020 = 60.20%)
//   寄存器 5: 光照                  (80000 lux)

class ModbusTCP {
public:
    // 构造函数：初始化 Modbus TCP 上下文
    // ip_address: 传感器设备的 IP（比如 "192.168.88.17"）
    // port: Modbus 标准端口（通常 502）
    ModbusTCP(const char* ip_address, int port) 
    {
        _ctx = modbus_new_tcp(ip_address, port);
        if (!_ctx) 
        {
            throw std::runtime_error("Failed to create Modbus context");
        }
    }

    ~ModbusTCP() 
    {
        if (_ctx) 
        {
            modbus_close(_ctx);
            modbus_free(_ctx);
        }
    }

    // 建立连接
    void connect() 
    {
        if (modbus_connect(_ctx) == -1) 
        {
            throw std::runtime_error(std::string("Connection failed: ") + modbus_strerror(errno));
        }
    }

    // 读取寄存器值
    // addr: 寄存器地址（从 0 开始）
    // nb: 要读多少个寄存器
    // dest: 存放结果的数组
    int readRegisters(int addr, int nb, uint16_t* dest) 
    {
        int rc = modbus_read_registers(_ctx, addr, nb, dest);
        if (rc == -1) 
        {
            throw std::runtime_error(std::string("Read failed: ") + modbus_strerror(errno));
        }
        return rc;
    }

    // 写入单个寄存器
    // reg_addr: 寄存器地址
    // value: 要写入的值
    void writeRegister(int reg_addr, const uint16_t value) 
    {
        if (modbus_write_register(_ctx, reg_addr, value) == -1) 
        {
            throw std::runtime_error(std::string("Write failed: ") + modbus_strerror(errno));
        }
    }

    // 写入多个寄存器
    // addr: 起始地址
    // nb: 要写多少个
    // data: 数据数组
    void writeRegisters(int addr, int nb, const uint16_t* data) 
    {
        if (modbus_write_registers(_ctx, addr, nb, data) == -1) 
        {
            throw std::runtime_error(std::string("Write multiple registers failed: ") + modbus_strerror(errno));
        }
    }

private:
    modbus_t* _ctx = nullptr;   //modbus上下文句柄
};