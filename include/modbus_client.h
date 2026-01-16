#pragma once

#include <cstdint>
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

private:
    bool readHoldingRegisters(uint16_t address, uint16_t count, uint16_t* values);

    modbus_t* ctx_;
    bool connected_;
    std::string server_ip_;
    uint16_t server_port_;
    uint8_t unit_id_;
    RegisterType register_type_ = RegisterType::Holding;
    bool debug_ = false;

    mutable std::mutex ctx_mutex_;
    mutable std::mutex variables_mutex_;
    std::map<std::string, ModbusVariable> variables_;
};
