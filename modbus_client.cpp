#include "modbus_client.h"
#include <iostream>
#include <cstring>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>

ModbusClient::ModbusClient()
    : socket_fd_(-1), connected_(false), server_port_(502) {
}

ModbusClient::~ModbusClient() {
    disconnect();
}

bool ModbusClient::connect(const std::string& ip, uint16_t port) {
    if (connected_) {
        disconnect();
    }

    server_ip_ = ip;
    server_port_ = port;

    // Создание сокета
    socket_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_fd_ < 0) {
        std::cerr << "Failed to create socket" << std::endl;
        return false;
    }

    // Настройка адреса сервера
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(server_port_);

    if (inet_pton(AF_INET, server_ip_.c_str(), &server_addr.sin_addr) <= 0) {
        std::cerr << "Invalid IP address: " << server_ip_ << std::endl;
        close(socket_fd_);
        socket_fd_ = -1;
        return false;
    }

    // Подключение
    if (::connect(socket_fd_, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        std::cerr << "Failed to connect to " << server_ip_ << ":" << server_port_ << std::endl;
        close(socket_fd_);
        socket_fd_ = -1;
        return false;
    }

    connected_ = true;
    std::cout << "Connected to Modbus TCP server " << server_ip_ << ":" << server_port_ << std::endl;
    return true;
}

void ModbusClient::disconnect() {
    if (socket_fd_ >= 0) {
        close(socket_fd_);
        socket_fd_ = -1;
    }
    connected_ = false;
}

void ModbusClient::registerVariable(const std::string& name, uint16_t address) {
    variables_[name] = ModbusVariable(address);
    std::cout << "Registered Modbus variable '" << name << "' at address " << address << std::endl;
}

bool ModbusClient::readVariables() {
    if (!connected_) {
        return false;
    }

    // Читаем каждую переменную по отдельности
    // В реальном приложении можно оптимизировать, читая диапазоны адресов
    for (auto& pair : variables_) {
        uint16_t value;
        if (readHoldingRegisters(pair.second.address, 1, &value)) {
            pair.second.value = value;
            pair.second.valid = true;
        } else {
            pair.second.valid = false;
        }
    }

    return true;
}

bool ModbusClient::getVariable(const std::string& name, uint16_t& value) {
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

    // Формирование Modbus TCP запроса
    uint8_t request[12];
    static uint16_t transaction_id = 0;

    // MBAP Header
    request[0] = (transaction_id >> 8) & 0xFF;  // Transaction ID high
    request[1] = transaction_id & 0xFF;         // Transaction ID low
    request[2] = 0;                             // Protocol ID high
    request[3] = 0;                             // Protocol ID low
    request[4] = 0;                             // Length high
    request[5] = 6;                             // Length low
    request[6] = 1;                             // Unit ID

    // PDU
    request[7] = 0x03;                          // Function code (Read Holding Registers)
    request[8] = (address >> 8) & 0xFF;         // Starting address high
    request[9] = address & 0xFF;                // Starting address low
    request[10] = (count >> 8) & 0xFF;          // Quantity high
    request[11] = count & 0xFF;                 // Quantity low

    transaction_id++;

    // Отправка запроса
    ssize_t sent = send(socket_fd_, request, sizeof(request), 0);
    if (sent != sizeof(request)) {
        std::cerr << "Failed to send Modbus request" << std::endl;
        return false;
    }

    // Получение ответа
    uint8_t response[256];
    ssize_t received = recv(socket_fd_, response, sizeof(response), 0);
    if (received < 9) {
        std::cerr << "Invalid Modbus response" << std::endl;
        return false;
    }

    // Проверка заголовка
    if (response[7] != 0x03) {
        std::cerr << "Invalid function code in response: " << (int)response[7] << std::endl;
        return false;
    }

    uint8_t byte_count = response[8];
    if (byte_count != count * 2) {
        std::cerr << "Invalid byte count in response" << std::endl;
        return false;
    }

    // Извлечение значений регистров
    for (uint16_t i = 0; i < count; i++) {
        values[i] = (response[9 + i * 2] << 8) | response[10 + i * 2];
    }

    return true;
}
