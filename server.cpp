#ifndef FD_SETSIZE
#define FD_SETSIZE 1024 // Define a larger size if needed, before including winsock2.h
#endif
#include <iostream>
#include <cassert>
#include <io.h>
#include <windows.h>
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <winsock2.h>
#include <ws2tcpip.h>
#include <vector>
#include <unordered_map>

#pragma include(lib, "Ws2_32.lib")

static void die(const char* message) {
    std::cout << "failed:" << message << std::endl;
}

static void msg(const char* message) {
    std::cout << message << std::endl;
}

static void do_something(SOCKET connfd) {
    char rbuf[64] = {};
    SSIZE_T n = recv(connfd, rbuf, sizeof(rbuf) - 1, 0);

    if (n < 0) { msg("read error"); return; }

    std::cout << "client says: " << rbuf << std::endl;

    char wbuf[] = "world";  
    send(connfd, wbuf, strlen(wbuf), 0);
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

    int val = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (const char*)&val, sizeof(val));

    struct sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(1234);        // port
    addr.sin_addr.s_addr = htonl(0);    // wildcard IP 0.0.0.0
    int rv = bind(fd, (const struct sockaddr *)&addr, sizeof(addr));
    if (rv) { die("bind()"); }

    rv = listen(fd, SOMAXCONN);
    if (rv) { die("listen()"); }

    while(true) {
        struct sockaddr_in client_addr = {};
        socklen_t addrlen = sizeof(client_addr);
        SOCKET connfd = accept(fd, (struct sockaddr *)&client_addr, &addrlen);
        if (connfd < 0) { continue; } // error

        do_something(connfd);
        closesocket(connfd);
    }

    closesocket(fd);
    WSACleanup();
    return 0;
}