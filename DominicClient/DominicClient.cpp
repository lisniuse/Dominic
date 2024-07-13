#define _WINSOCK_DEPRECATED_NO_WARNINGS // 禁用弃用 API 警告

#include <winsock2.h>
#include <ws2tcpip.h>
#include <iostream>
#include <string>
#include <thread>
#include <chrono>
#include <stdexcept>

#pragma comment(lib, "ws2_32.lib")

void handleInput(SOCKET sock) {
    while (true) {
        std::string command;
        std::cout << "Enter command: ";
        std::getline(std::cin, command);

        if (command.empty()) continue;

        // 发送命令到被控制端
        send(sock, command.c_str(), command.length(), 0);

        // 设置超时时间（例如 10 秒）
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(sock, &readfds);

        timeval timeout;
        timeout.tv_sec = 10;
        timeout.tv_usec = 0;

        int selectResult = select(0, &readfds, NULL, NULL, &timeout);

        if (selectResult > 0 && FD_ISSET(sock, &readfds)) {
            char buffer[1024];
            int bytesReceived = recv(sock, buffer, sizeof(buffer) - 1, 0);
            if (bytesReceived > 0) {
                buffer[bytesReceived] = '\0';
                std::cout << "Response: " << buffer << std::endl;
            }
        }
        else {
            std::cout << "Timeout! No response received." << std::endl;
        }
    }
}

int main(int argc, char* argv[]) {
    if (argc != 5) {
        std::cerr << "\nUsage: " << argv[0] << " -ip <IP> -port <PORT>" << std::endl;
        return 1;
    }

    std::string ip;
    int port = 0;

    for (int i = 1; i < argc; i += 2) {
        std::string arg = argv[i];
        if (arg == "-ip" || arg == "-I") {
            ip = argv[i + 1];
        }
        else if (arg == "-port" || arg == "-P") {
            try {
                port = std::stoi(argv[i + 1]);
            }
            catch (const std::invalid_argument&) {
                std::cerr << "Invalid port number!" << std::endl;
                return 1;
            }
        }
    }

    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "WSAStartup failed!" << std::endl;
        return 1;
    }

    SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    sockaddr_in serverAddr = {};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = inet_addr(ip.c_str()); // 使用 inet_addr
    serverAddr.sin_port = htons(port);

    if (connect(sock, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
        std::cerr << "Connection failed!" << std::endl;
        return 1;
    }

    std::thread inputThread(handleInput, sock);

    inputThread.join();

    closesocket(sock);
    WSACleanup();

    return 0;
}
