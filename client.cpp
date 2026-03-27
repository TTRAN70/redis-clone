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

static int32_t read_full(SOCKET fd, char *buf, size_t n) {
    while (n > 0) {
        SSIZE_T rv = recv(fd, buf, n, 0);
        if (rv <= 0) {
            return -1;  // error, or unexpected EOF
        }
        assert((size_t)rv <= n);
        n -= (size_t)rv;
        buf += rv;
    }
    return 0;
}

static int32_t write_all(SOCKET fd, const char *buf, size_t n) {
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

static int32_t send_req(SOCKET fd, const std::vector<std::string> &cmd) {
    uint32_t len = 4;
    for (const std::string &s : cmd) {
        len += 4 + s.size();
    }
    if (len > k_max_msg) {
        return -1;
    }

    char wbuf[4 + k_max_msg];
    memcpy(&wbuf[0], &len, 4);  // assume little endian
    uint32_t n = cmd.size();
    memcpy(&wbuf[4], &n, 4);
    size_t cur = 8;
    for (const std::string &s : cmd) {
        uint32_t p = (uint32_t)s.size();
        memcpy(&wbuf[cur], &p, 4);
        memcpy(&wbuf[cur + 4], s.data(), s.size());
        cur += 4 + s.size();
    }
    return write_all(fd, wbuf, 4 + len);
}

enum {
    TAG_NIL = 0,    // nil
    TAG_ERR = 1,    // error code + msg
    TAG_STR = 2,    // string
    TAG_INT = 3,    // int64
    TAG_DBL = 4,    // double
    TAG_ARR = 5,    // array
};

static int32_t print_response(const uint8_t *data, size_t size) {
    if (size < 1) {
        msg("bad response");
        return -1;
    }
    switch (data[0]) {
    case TAG_NIL:
        printf("(nil)\n");
        return 1;
    case TAG_ERR:
        if (size < 1 + 8) {
            msg("bad response");
            return -1;
        }
        {
            int32_t code = 0;
            uint32_t len = 0;
            memcpy(&code, &data[1], 4);
            memcpy(&len, &data[1 + 4], 4);
            if (size < 1 + 8 + len) {
                msg("bad response");
                return -1;
            }
            printf("(err) %d %.*s\n", code, len, &data[1 + 8]);
            return 1 + 8 + len;
        }
    case TAG_STR:
        if (size < 1 + 4) {
            msg("bad response");
            return -1;
        }
        {
            uint32_t len = 0;
            memcpy(&len, &data[1], 4);
            if (size < 1 + 4 + len) {
                msg("bad response");
                return -1;
            }
            printf("(str) %.*s\n", len, &data[1 + 4]);
            return 1 + 4 + len;
        }
    case TAG_INT:
        if (size < 1 + 8) {
            msg("bad response");
            return -1;
        }
        {
            int64_t val = 0;
            memcpy(&val, &data[1], 8);
            printf("(int) %ld\n", val);
            return 1 + 8;
        }
    case TAG_DBL:
        if (size < 1 + 8) {
            msg("bad response");
            return -1;
        }
        {
            double val = 0;
            memcpy(&val, &data[1], 8);
            printf("(dbl) %g\n", val);
            return 1 + 8;
        }
    case TAG_ARR:
        if (size < 1 + 4) {
            msg("bad response");
            return -1;
        }
        {
            uint32_t len = 0;
            memcpy(&len, &data[1], 4);
            printf("(arr) len=%u\n", len);
            size_t arr_bytes = 1 + 4;
            for (uint32_t i = 0; i < len; ++i) {
                int32_t rv = print_response(&data[arr_bytes], size - arr_bytes);
                if (rv < 0) {
                    return rv;
                }
                arr_bytes += (size_t)rv;
            }
            printf("(arr) end\n");
            return (int32_t)arr_bytes;
        }
    default:
        msg("bad response");
        return -1;
    }
}

static int32_t read_res(SOCKET fd) {
    // 4 bytes header
    char rbuf[4 + k_max_msg];
    errno = 0;
    int32_t err = read_full(fd, rbuf, 4);
    if (err) {
        if (errno == 0) {
            msg("EOF");
        } else {
            msg("read() error");
        }
        return err;
    }

    uint32_t len = 0;
    memcpy(&len, rbuf, 4);  // assume little endian
    if (len > k_max_msg) {
        msg("too long");
        return -1;
    }

    // reply body
    err = read_full(fd, &rbuf[4], len);
    if (err) {
        msg("read() error");
        return err;
    }

    // print the result
    int32_t rv = print_response((uint8_t *)&rbuf[4], len);
    if (rv > 0 && (uint32_t)rv != len) {
        msg("bad response");
        rv = -1;
    }
    return rv;
}

int main(int argc, char **argv) {   
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

    std::vector<std::string> cmd;
    for (int i = 1; i < argc; ++i) {
        cmd.push_back(argv[i]);
    }
    int32_t err = send_req(fd, cmd);
    if (err) {
        goto L_DONE;
    }
    err = read_res(fd);
    if (err) {
        goto L_DONE;
    }

L_DONE:
    close(fd);
    return 0;
}