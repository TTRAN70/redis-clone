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

const size_t k_max_msg = 4096;

static void die(const char* message) {
    std::cout << "failed:" << message << std::endl;
}

static void msg(const char* message) {
    std::cout << message << std::endl;
}

static int32_t read_full(SOCKET fd, char* rbuf, size_t n) {
    while (n > 0) {
        SSIZE_T rv = recv(fd, rbuf, n, 0);
        if (rv <= 0) { return -1; } // error or unexpected EOF
        
        assert((size_t)rv <= n);
        n -= (size_t)rv;
        rbuf += rv;
    }
    return 0;
}

static int32_t write_all(SOCKET fd, char* wbuf, size_t n) {
    while (n > 0) {
        SSIZE_T rv = send(fd, wbuf, n, 0);
        if (rv <= 0) { return -1; } // error
        
        assert((size_t)rv <= n);
        n -= (size_t)rv;
        wbuf += rv;
    }
    return 0;
}

static int32_t one_request(SOCKET connfd) {
    char rbuf[4 + k_max_msg];
    int32_t err = read_full(connfd, rbuf, 4);
    errno = 0;
    if (err) {
        msg(errno == 0 ? "EOF" : "read() error");
        return err;
    }

    uint32_t len = 0;
    memcpy(&len, rbuf, 4);
    if (len > k_max_msg) { msg("too long"); return -1; }

    err = read_full(connfd, &rbuf[4], len);
    if (err) {
        msg("read() error");
        return err;
    }

    std::cout << "client says: " << &rbuf[4] << std::endl;

    const char reply[] = "world";
    char wbuf[4 + sizeof(reply)];
    len = (uint32_t)strlen(reply);
    memcpy(wbuf, &len, 4);
    memcpy(&wbuf[4], reply, len);
    return write_all(connfd, wbuf, 4 + len);
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

        // only serves one request at once
        while(true) {
            int32_t err = one_request(connfd);
            if (err) { break; }
        }
        closesocket(connfd);
    }

    closesocket(fd);
    WSACleanup();
    return 0;
}