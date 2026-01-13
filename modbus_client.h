#pragma once

#include <string>
#include <cstdint>
#include <map>

// Структура для хранения значения переменной Modbus
struct ModbusVariable {
    uint16_t address;       // Адрес регистра
    uint16_t value;         // Значение регистра
    bool valid;             // Флаг валидности данных

    ModbusVariable(uint16_t addr = 0) : address(addr), value(0), valid(false) {}
};

// Класс для работы с Modbus TCP
class ModbusClient {
public:
    ModbusClient();
    ~ModbusClient();

    // Подключение к Modbus TCP серверу
    bool connect(const std::string& ip, uint16_t port = 502);

    // Отключение
    void disconnect();

    // Проверка подключения
    bool isConnected() const { return connected_; }

    // Регистрация переменной для чтения
    void registerVariable(const std::string& name, uint16_t address);

    // Чтение всех зарегистрированных переменных
    bool readVariables();

    // Получение значения переменной
    bool getVariable(const std::string& name, uint16_t& value);

    // Получение строкового представления переменной
    std::string getVariableString(const std::string& name);

private:
    bool readHoldingRegisters(uint16_t address, uint16_t count, uint16_t* values);

    int socket_fd_;
    bool connected_;
    std::string server_ip_;
    uint16_t server_port_;

    std::map<std::string, ModbusVariable> variables_;
};
