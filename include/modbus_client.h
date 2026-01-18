#pragma once

#include <cstdint>
#include <atomic>
#include <map>
#include <mutex>
#include <string>

typedef struct _modbus modbus_t;

// Описание переменной Modbus
struct ModbusVariable {
    uint16_t address;
    uint16_t value;
    bool valid;

    ModbusVariable(uint16_t addr = 0) : address(addr), value(0), valid(false) {}
};

// Modbus TCP клиент на libmodbus
class ModbusClient {
public:
    enum class RegisterType {
        Holding,
        Input
    };

    ModbusClient();
    ~ModbusClient();

    void setUnitId(uint8_t unit_id);
    void setRegisterType(RegisterType type);
    void setRegisterType(const std::string& type);
    void setDebug(bool enable);
    void setAddressOffset(int offset);
    void setBlockRead(bool enable);
    void setResponseTimeoutMs(int timeout_ms);
    void setByteTimeoutMs(int timeout_ms);
    void setInterRequestDelayMs(int delay_ms);
    void setLogTimestamps(bool enable);
    void setKeepLastOnError(bool enable);
    void setResetAfterErrors(int count);

    // Подключение к серверу Modbus TCP
    bool connect(const std::string& ip, uint16_t port = 502);

    // Отключение
    void disconnect();

    // Проверка подключения
    bool isConnected() const { return connected_; }

    // Зарегистрировать переменную по адресу
    void registerVariable(const std::string& name, uint16_t address);

    // Прочитать все зарегистрированные переменные
    bool readVariables();

    // Получить значение переменной
    bool getVariable(const std::string& name, uint16_t& value);

    // Получить значение как строку
    std::string getVariableString(const std::string& name);
    bool lastErrorIsConnection() const;
    uint32_t getErrorCount() const;

private:
    bool readHoldingRegisters(uint16_t address, uint16_t count, uint16_t* values);

    modbus_t* ctx_;
    bool connected_;
    std::string server_ip_;
    uint16_t server_port_;
    uint8_t unit_id_;
    RegisterType register_type_ = RegisterType::Holding;
    bool debug_ = false;
    int address_offset_ = 0;
    bool block_read_ = true;
    int response_timeout_ms_ = 500;
    int byte_timeout_ms_ = 500;
    int inter_request_delay_ms_ = 0;
    bool log_timestamps_ = false;
    bool keep_last_on_error_ = true;
    int reset_after_errors_ = 0;
    int consecutive_errors_ = 0;

    mutable std::mutex ctx_mutex_;
    mutable std::mutex variables_mutex_;
    std::map<std::string, ModbusVariable> variables_;
    int last_errno_ = 0;
    std::atomic<uint32_t> error_count_{0};
};
