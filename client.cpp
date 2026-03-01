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

const size_t k_max_msg = 32 << 20;

static void die(const char* message) {
    std::cout << "failed:" << message << std::endl;
}

static void msg(const char* message) {
    std::cout << message << std::endl;
}

static int32_t read_full(SOCKET fd, uint8_t *buf, size_t n) {
    while (n > 0) {
        SSIZE_T rv = recv(fd, (char *)buf, n, 0);
        if (rv <= 0) {
            return -1;  // error, or unexpected EOF
        }
        assert((size_t)rv <= n);
        n -= (size_t)rv;
        buf += rv;
    }
    return 0;
}

static int32_t write_all(SOCKET fd, const uint8_t *buf, size_t n) {
    while (n > 0) {
        SSIZE_T rv = send(fd, (char *)buf, n, 0);
        if (rv <= 0) {
            return -1;  // error
        }
        assert((size_t)rv <= n);
        n -= (size_t)rv;
        buf += rv;
    }
    return 0;
}

// append to the back
static void buf_append(std::vector<uint8_t> &buf, const uint8_t *data, size_t len) {
    buf.insert(buf.end(), data, data + len);
}

// the `query` function was simply splited into `send_req` and `read_res`.
static int32_t send_req(SOCKET fd, const uint8_t *text, size_t len) {
    if (len > k_max_msg) {
        return -1;
    }

    std::vector<uint8_t> wbuf;
    buf_append(wbuf, (const uint8_t *)&len, 4);
    buf_append(wbuf, text, len);
    return write_all(fd, wbuf.data(), wbuf.size());
}

static int32_t read_res(SOCKET fd) {
    // 4 bytes header
    std::vector<uint8_t> rbuf;
    rbuf.resize(4);
    errno = 0;
    int32_t err = read_full(fd, &rbuf[0], 4);
    if (err) {
        if (errno == 0) {
            msg("EOF");
        } else {
            msg("read() error");
        }
        return err;
    }

    uint32_t len = 0;
    memcpy(&len, rbuf.data(), 4);  // assume little endian
    if (len > k_max_msg) {
        msg("too long");
        return -1;
    }

    // reply body
    rbuf.resize(4 + len);
    err = read_full(fd, &rbuf[4], len);
    if (err) {
        msg("read() error");
        return err;
    }

    // do something
    rbuf.push_back('\0');
    std::cout << "server says: " << &rbuf[4] << std::endl;
    rbuf.pop_back();
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

    // multiple pipelined requests
    std::vector<std::string> query_list = {
        "hello1", "hello2", "hello3",
        // a large message requires multiple event loop iterations
        std::string(50, 'z'),
        "hello5",
    };
    for (const std::string &s : query_list) {
        int32_t err = send_req(fd, (uint8_t *)s.data(), s.size());
        if (err) {
            goto L_DONE;
        }
    }
    for (size_t i = 0; i < query_list.size(); ++i) {
        int32_t err = read_res(fd);
        if (err) {
            goto L_DONE;
        }
    }

    L_DONE:
    closesocket(fd);
    WSACleanup();

    return 0;
}