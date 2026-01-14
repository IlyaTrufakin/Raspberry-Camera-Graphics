#include "modbus_client.h"

#include <cerrno>
#include <iostream>
#include <limits>
#include <mutex>
#include <vector>

#include <modbus/modbus.h>

ModbusClient::ModbusClient()
    : ctx_(nullptr), connected_(false), server_port_(502), unit_id_(1) {
}

ModbusClient::~ModbusClient() {
    disconnect();
}

void ModbusClient::setUnitId(uint8_t unit_id) {
    unit_id_ = unit_id;
}

bool ModbusClient::connect(const std::string& ip, uint16_t port) {
    if (connected_) {
        disconnect();
    }

    server_ip_ = ip;
    server_port_ = port;

    ctx_ = modbus_new_tcp(server_ip_.c_str(), server_port_);
    if (!ctx_) {
        std::cerr << "Failed to create Modbus context" << std::endl;
        return false;
    }

    if (modbus_set_slave(ctx_, unit_id_) == -1) {
        std::cerr << "Failed to set Modbus unit id " << static_cast<int>(unit_id_)
                  << " error=" << modbus_strerror(errno) << std::endl;
        modbus_free(ctx_);
        ctx_ = nullptr;
        return false;
    }

    modbus_set_response_timeout(ctx_, 0, 200000);
    modbus_set_byte_timeout(ctx_, 0, 200000);

    if (modbus_connect(ctx_) == -1) {
        std::cerr << "Failed to connect to " << server_ip_ << ":" << server_port_
                  << " error=" << modbus_strerror(errno) << std::endl;
        modbus_free(ctx_);
        ctx_ = nullptr;
        return false;
    }

    connected_ = true;
    std::cout << "Connected to Modbus TCP server " << server_ip_ << ":" << server_port_ << std::endl;
    return true;
}

void ModbusClient::disconnect() {
    std::lock_guard<std::mutex> lock(ctx_mutex_);
    if (ctx_) {
        modbus_close(ctx_);
        modbus_free(ctx_);
        ctx_ = nullptr;
    }
    connected_ = false;
}

void ModbusClient::registerVariable(const std::string& name, uint16_t address) {
    std::lock_guard<std::mutex> lock(variables_mutex_);
    variables_[name] = ModbusVariable(address);
    std::cout << "Registered Modbus variable '" << name << "' at address " << address << std::endl;
}

bool ModbusClient::readVariables() {
    if (!connected_) {
        return false;
    }

    std::vector<std::pair<std::string, ModbusVariable>> vars_copy;
    {
        std::lock_guard<std::mutex> lock(variables_mutex_);
        if (variables_.empty()) {
            return true;
        }
        vars_copy.reserve(variables_.size());
        for (const auto& pair : variables_) {
            vars_copy.push_back(pair);
        }
    }

    uint16_t min_addr = std::numeric_limits<uint16_t>::max();
    uint16_t max_addr = 0;
    for (const auto& pair : vars_copy) {
        min_addr = std::min(min_addr, pair.second.address);
        max_addr = std::max(max_addr, pair.second.address);
    }

    uint16_t count = static_cast<uint16_t>(max_addr - min_addr + 1);
    if (count > 0 && count <= 125) {
        std::vector<uint16_t> values(count, 0);
        if (readHoldingRegisters(min_addr, count, values.data())) {
            std::lock_guard<std::mutex> lock(variables_mutex_);
            for (auto& pair : variables_) {
                uint16_t offset = static_cast<uint16_t>(pair.second.address - min_addr);
                pair.second.value = values[offset];
                pair.second.valid = true;
            }
            return true;
        }
    }

    bool any_ok = false;
    {
        std::lock_guard<std::mutex> lock(variables_mutex_);
        for (auto& pair : variables_) {
            pair.second.valid = false;
        }
    }

    for (const auto& pair : vars_copy) {
        uint16_t value;
        if (readHoldingRegisters(pair.second.address, 1, &value)) {
            std::lock_guard<std::mutex> lock(variables_mutex_);
            auto it = variables_.find(pair.first);
            if (it != variables_.end()) {
                it->second.value = value;
                it->second.valid = true;
            }
            any_ok = true;
        }
    }

    return any_ok;
}

bool ModbusClient::getVariable(const std::string& name, uint16_t& value) {
    std::lock_guard<std::mutex> lock(variables_mutex_);
    auto it = variables_.find(name);
    if (it == variables_.end() || !it->second.valid) {
        return false;
    }

    value = it->second.value;
    return true;
}

std::string ModbusClient::getVariableString(const std::string& name) {
    uint16_t value;
    if (getVariable(name, value)) {
        return std::to_string(value);
    }
    return "---";
}

bool ModbusClient::readHoldingRegisters(uint16_t address, uint16_t count, uint16_t* values) {
    if (!connected_ || count == 0 || count > 125) {
        return false;
    }

    std::lock_guard<std::mutex> lock(ctx_mutex_);
    int rc = modbus_read_registers(ctx_, address, count, values);
    if (rc != count) {
        std::cerr << "Modbus read failed: addr=" << address
                  << " count=" << count
                  << " error=" << modbus_strerror(errno) << std::endl;
        return false;
    }

    return true;
}
