#include <iostream>
#include <cassert>
#include <io.h>
#include <windows.h>
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <vector>
#include <string>
#pragma comment(lib, "Ws2_32.lib")

static void die(const char* message) {
    std::cout << "failed:" << message << std::endl;
}

static void msg(const char* message) {
    std::cout << message << std::endl;
}

int main() {   
    WSADATA wsaData = {0};
    int iResult = 0;

    // Initialize Winsock
    iResult = WSAStartup(MAKEWORD(2,2), &wsaData);
    if (iResult != 0) {
        // Handle error
        printf("WSAStartup failed: %d\n", iResult);
        return 1;
    }  

    SOCKET fd = socket(AF_INET, SOCK_STREAM, 0);

    if (fd < 0) { die("socket()"); }

    struct sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = ntohs(1234);        // port
    addr.sin_addr.s_addr = ntohl(INADDR_LOOPBACK); // wildcard IP 0.0.0.0

    int rv = connect(fd, (const sockaddr*)&addr, sizeof(addr));
    if (rv) { die("connect()"); }

    char wbuf[] = "hello";
    send(fd, wbuf, strlen(wbuf), 0);

    char rbuf[64] = {};
    SSIZE_T n = recv(fd, rbuf, sizeof(rbuf) - 1, 0);
    if (n < 0) { die("read()"); }

    std::cout << "server says: " << rbuf << std::endl;
    closesocket(fd);
    WSACleanup();

    return 0;
}