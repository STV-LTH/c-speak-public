#pragma once

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstring>
#include <string>
#include <iostream>
#include <vector>
#include <thread>
#include <atomic>

class Network {
public:
    Network() : sockfd(-1), running(false) {}

    ~Network() { stop(); }

    // Серверный режим (слушаем порт)
    bool start_server(int port) {
        return create_socket("0.0.0.0", port);
    }

    // Клиентский режим (подключаемся к серверу)
    bool start_client(const std::string& server_ip, int port) {
        peer_addr.sin_family = AF_INET;
        peer_addr.sin_port = htons(port);
        inet_pton(AF_INET, server_ip.c_str(), &peer_addr.sin_addr);

        return create_socket("0.0.0.0", 0); // Слушаем на любом порту
    }

    void stop() {
        running = false;
        if (sockfd != -1) {
            close(sockfd);
            sockfd = -1;
        }
    }

    // Отправка данных
    bool send(const std::vector<unsigned char>& data) {
        if (sockfd == -1) return false;

        socklen_t addr_len = sizeof(peer_addr);
        int sent = sendto(sockfd, data.data(), data.size(), 0,
                         (struct sockaddr*)&peer_addr, addr_len);

        return sent == static_cast<int>(data.size());
    }

    // Получение данных (неблокирующее)
    bool receive(std::vector<unsigned char>& data) {
        if (sockfd == -1) return false;

        char buffer[4096];
        sockaddr_in from_addr;
        socklen_t addr_len = sizeof(from_addr);

        int received = recvfrom(sockfd, buffer, sizeof(buffer), MSG_DONTWAIT,
                               (struct sockaddr*)&from_addr, &addr_len);

        if (received > 0) {
            data.assign(buffer, buffer + received);

            // Автоматически обновляем peer для ответа
            if (peer_addr.sin_port == 0) {
                peer_addr = from_addr;
            }

            return true;
        }

        return false;
    }

private:
    bool create_socket(const std::string& bind_ip, int port) {
        sockfd = socket(AF_INET, SOCK_DGRAM, 0);
        if (sockfd < 0) return false;

        // Неблокирующий режим
        int flags = fcntl(sockfd, F_GETFL, 0);
        fcntl(sockfd, F_SETFL, flags | O_NONBLOCK);

        // Биндим сокет
        sockaddr_in bind_addr;
        memset(&bind_addr, 0, sizeof(bind_addr));
        bind_addr.sin_family = AF_INET;
        bind_addr.sin_port = htons(port);
        inet_pton(AF_INET, bind_ip.c_str(), &bind_addr.sin_addr);

        if (bind(sockfd, (struct sockaddr*)&bind_addr, sizeof(bind_addr)) < 0) {
            close(sockfd);
            sockfd = -1;
            return false;
        }

        running = true;
        return true;
    }

private:
    int sockfd;
    std::atomic<bool> running;
    sockaddr_in peer_addr;
};
