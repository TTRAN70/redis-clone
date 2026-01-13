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

static int32_t query(SOCKET fd, const char* text) {
    uint32_t len = (uint32_t)strlen(text);
    if (len > k_max_msg) { return -1; }
    char wbuf[4 + k_max_msg];
    memcpy(wbuf, &len, 4);
    memcpy(&wbuf[4], text, len);
    int32_t err = write_all(fd, wbuf, 4 + len);
    if (err) {
        msg("write() error");
        return err;
    }

    char rbuf[4 + k_max_msg];
    err = read_full(fd, rbuf, 4);
    errno = 0;
    if (err) {
        msg(errno == 0 ? "EOF" : "read() error");
        return err;
    }

    len = 0;
    memcpy(&len, rbuf, 4);
    if (len > k_max_msg) { msg("too long"); return -1; }

    err = read_full(fd, &rbuf[4], len);
    if (err) {
        msg("read() error");
        return err;
    }

    std::cout << "server says: " << &rbuf[4] << std::endl;

    return 0;
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

    if (query(fd, "hello1")) goto L_DONE;
    if (query(fd, "hello2")) goto L_DONE;
    if (query(fd, "hello3")) goto L_DONE;

    L_DONE:
    closesocket(fd);
    WSACleanup();

    return 0;
}