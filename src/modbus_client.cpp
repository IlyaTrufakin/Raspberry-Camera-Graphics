#include "modbus_client.h"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <cctype>
#include <cerrno>
#include <iostream>
#include <limits>
#include <mutex>
#include <sstream>
#include <thread>
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

void ModbusClient::setRegisterType(RegisterType type) {
    register_type_ = type;
}

void ModbusClient::setRegisterType(const std::string& type) {
    std::string v = type;
    std::transform(v.begin(), v.end(), v.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (v == "input") {
        register_type_ = RegisterType::Input;
    } else if (v == "holding") {
        register_type_ = RegisterType::Holding;
    }
}

void ModbusClient::setDebug(bool enable) {
    debug_ = enable;
    if (ctx_) {
        modbus_set_debug(ctx_, debug_ ? 1 : 0);
    }
}

void ModbusClient::setAddressOffset(int offset) {
    address_offset_ = offset;
}

void ModbusClient::setBlockRead(bool enable) {
    block_read_ = enable;
}

void ModbusClient::setResponseTimeoutMs(int timeout_ms) {
    response_timeout_ms_ = timeout_ms;
    if (ctx_) {
        modbus_set_response_timeout(ctx_, response_timeout_ms_ / 1000,
                                    (response_timeout_ms_ % 1000) * 1000);
    }
}

void ModbusClient::setByteTimeoutMs(int timeout_ms) {
    byte_timeout_ms_ = timeout_ms;
    if (ctx_) {
        modbus_set_byte_timeout(ctx_, byte_timeout_ms_ / 1000,
                                (byte_timeout_ms_ % 1000) * 1000);
    }
}

void ModbusClient::setInterRequestDelayMs(int delay_ms) {
    inter_request_delay_ms_ = delay_ms;
}

void ModbusClient::setLogTimestamps(bool enable) {
    log_timestamps_ = enable;
}

static std::string formatTimestamp() {
    auto now = std::chrono::system_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    std::ostringstream oss;
    oss << std::put_time(&tm, "%H:%M:%S") << "." << std::setw(3) << std::setfill('0') << ms.count();
    return oss.str();
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
        last_errno_ = errno;
        return false;
    }

    modbus_set_debug(ctx_, debug_ ? 1 : 0);

    if (modbus_set_slave(ctx_, unit_id_) == -1) {
        std::cerr << "Failed to set Modbus unit id " << static_cast<int>(unit_id_)
                  << " error=" << modbus_strerror(errno) << std::endl;
        last_errno_ = errno;
        modbus_free(ctx_);
        ctx_ = nullptr;
        return false;
    }

    modbus_set_response_timeout(ctx_, response_timeout_ms_ / 1000,
                                (response_timeout_ms_ % 1000) * 1000);
    modbus_set_byte_timeout(ctx_, byte_timeout_ms_ / 1000,
                            (byte_timeout_ms_ % 1000) * 1000);

    if (modbus_connect(ctx_) == -1) {
        std::cerr << "Failed to connect to " << server_ip_ << ":" << server_port_
                  << " error=" << modbus_strerror(errno) << std::endl;
        last_errno_ = errno;
        modbus_free(ctx_);
        ctx_ = nullptr;
        return false;
    }

    connected_ = true;
    last_errno_ = 0;
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
    if (block_read_ && count > 0 && count <= 125) {
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
        if (inter_request_delay_ms_ > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(inter_request_delay_ms_));
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
        if (!connected_) {
            last_errno_ = ENOTCONN;
        }
        return false;
    }

    uint16_t read_address = address;
    if (address_offset_ != 0) {
        int adjusted = static_cast<int>(address) + address_offset_;
        if (adjusted < 0 || adjusted > std::numeric_limits<uint16_t>::max()) {
            last_errno_ = ERANGE;
            return false;
        }
        read_address = static_cast<uint16_t>(adjusted);
    }

    std::lock_guard<std::mutex> lock(ctx_mutex_);
    int rc = 0;
    int func = 0;
    if (register_type_ == RegisterType::Input) {
        rc = modbus_read_input_registers(ctx_, read_address, count, values);
        func = 4;
    } else {
        rc = modbus_read_registers(ctx_, read_address, count, values);
        func = 3;
    }
    if (rc != count) {
        if (log_timestamps_) {
            std::cerr << "[" << formatTimestamp() << "] ";
        }
        std::cerr << "Modbus read failed: addr=" << address
                  << " count=" << count
                  << " func=" << func
                  << " error=" << modbus_strerror(errno) << std::endl;
        last_errno_ = errno;
        if (last_errno_ == ETIMEDOUT) {
            modbus_flush(ctx_);
        }
        return false;
    }

    if (log_timestamps_) {
        std::cout << "[" << formatTimestamp() << "] "
                  << "Modbus read ok: addr=" << address
                  << " count=" << count
                  << " func=" << func << std::endl;
    }
    last_errno_ = 0;
    return true;
}

bool ModbusClient::lastErrorIsConnection() const {
    int err = last_errno_;
    if (err == 0) {
        return false;
    }
    if (err >= MODBUS_ENOBASE) {
        return false;
    }
    return err == ETIMEDOUT ||
           err == ECONNRESET ||
           err == EPIPE ||
           err == ECONNABORTED ||
           err == ENOTCONN ||
           err == ECONNREFUSED ||
           err == EHOSTUNREACH ||
           err == ENETUNREACH;
}
