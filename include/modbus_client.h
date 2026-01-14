#pragma once

#include <cstdint>
#include <map>
#include <string>

// Описание переменной Modbus
struct ModbusVariable {
    uint16_t address;
    uint16_t value;
    bool valid;

    ModbusVariable(uint16_t addr = 0) : address(addr), value(0), valid(false) {}
};

// Простой Modbus TCP клиент
class ModbusClient {
public:
    ModbusClient();
    ~ModbusClient();

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

    int socket_fd_;
    bool connected_;
    std::string server_ip_;
    uint16_t server_port_;

    std::map<std::string, ModbusVariable> variables_;
};
