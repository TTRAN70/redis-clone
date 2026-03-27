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
#include <map>
// proj
#include "hashtable.h"

#pragma include(lib, "Ws2_32.lib")

#define container_of(ptr, T, member) \
    ((T *)( (char *)ptr - offsetof(T, member) ))

const size_t k_max_msg = 32 << 20;

typedef std::vector<uint8_t> Buffer;

struct Conn {
    SOCKET fd = -1;
    bool want_read = false;
    bool want_write = false;
    bool want_close = false;
    std::vector<uint8_t> incoming;
    std::vector<uint8_t> outgoing;
};

// Response::status
enum {
    RES_OK = 0,
    RES_ERR = 1,    // error
    RES_NX = 2,     // key not found
};

// +--------+---------+
// | status | data... |
// +--------+---------+
struct Response {
    uint32_t status = 0;
    std::vector<uint8_t> data;
};

static struct {
    HMap db;    // top-level hashtable
} g_data;

// KV pair for the top-level hashtable
struct Entry {
    struct HNode node;  // hashtable node
    std::string key;
    std::string val;
};

static bool entry_eq(HNode *lhs, HNode *rhs) {
    struct Entry *le = container_of(lhs, struct Entry, node);
    struct Entry *re = container_of(rhs, struct Entry, node);
    return le->key == re->key;
}

// append to the back
static void buf_append(std::vector<uint8_t> &buf, const uint8_t *data, size_t len) {
    buf.insert(buf.end(), data, data + len);
}
// remove from the front
static void buf_consume(std::vector<uint8_t> &buf, size_t n) {
    buf.erase(buf.begin(), buf.begin() + n);
}


// error code for TAG_ERR
enum {
    ERR_UNKNOWN = 1,    // unknown command
    ERR_TOO_BIG = 2,    // response too big
};

// data types of serialized data
enum {
    TAG_NIL = 0,    // nil
    TAG_ERR = 1,    // error code + msg
    TAG_STR = 2,    // string
    TAG_INT = 3,    // int64
    TAG_DBL = 4,    // double
    TAG_ARR = 5,    // array
};

// helper functions for the serialization
static void buf_append_u8(Buffer &buf, uint8_t data) {
    buf.push_back(data);
}
static void buf_append_u32(Buffer &buf, uint32_t data) {
    buf_append(buf, (const uint8_t *)&data, 4);
}
static void buf_append_i64(Buffer &buf, int64_t data) {
    buf_append(buf, (const uint8_t *)&data, 8);
}
static void buf_append_dbl(Buffer &buf, double data) {
    buf_append(buf, (const uint8_t *)&data, 8);
}

// append serialized data types to the back
static void out_nil(Buffer &out) {
    buf_append_u8(out, TAG_NIL);
}
static void out_str(Buffer &out, const char *s, size_t size) {
    buf_append_u8(out, TAG_STR);
    buf_append_u32(out, (uint32_t)size);
    buf_append(out, (const uint8_t *)s, size);
}
static void out_int(Buffer &out, int64_t val) {
    buf_append_u8(out, TAG_INT);
    buf_append_i64(out, val);
}
static void out_dbl(Buffer &out, double val) {
    buf_append_u8(out, TAG_DBL);
    buf_append_dbl(out, val);
}
static void out_err(Buffer &out, uint32_t code, const std::string &msg) {
    buf_append_u8(out, TAG_ERR);
    buf_append_u32(out, code);
    buf_append_u32(out, (uint32_t)msg.size());
    buf_append(out, (const uint8_t *)msg.data(), msg.size());
}
static void out_arr(Buffer &out, uint32_t n) {
    buf_append_u8(out, TAG_ARR);
    buf_append_u32(out, n);
}

// FNV hash
static uint64_t str_hash(const uint8_t *data, size_t len) {
    uint32_t h = 0x811C9DC5;
    for (size_t i = 0; i < len; i++) {
        h = (h + data[i]) * 0x01000193;
    }
    return h;
}

static void do_get(std::vector<std::string> &cmd, Buffer &out) {
    // a dummy `Entry` just for the lookup
    Entry key;
    key.key.swap(cmd[1]);
    key.node.hcode = str_hash((uint8_t *)key.key.data(), key.key.size());
    // hashtable lookup
    HNode *node = hm_lookup(&g_data.db, &key.node, &entry_eq);
    if (!node) {
        return out_nil(out);
    }
    // copy the value
    const std::string &val = container_of(node, Entry, node)->val;
    return out_str(out, val.data(), val.size());
}

static void do_set(std::vector<std::string> &cmd, Buffer &out) {
    // a dummy `Entry` just for the lookup
    Entry key;
    key.key.swap(cmd[1]);
    key.node.hcode = str_hash((uint8_t *)key.key.data(), key.key.size());
    // hashtable lookup
    HNode *node = hm_lookup(&g_data.db, &key.node, &entry_eq);
    if (node) {
        // found, update the value
        container_of(node, Entry, node)->val.swap(cmd[2]);
    } else {
        // not found, allocate & insert a new pair
        Entry *ent = new Entry();
        ent->key.swap(key.key);
        ent->node.hcode = key.node.hcode;
        ent->val.swap(cmd[2]);
        hm_insert(&g_data.db, &ent->node);
    }
    return out_nil(out);
}

static void do_del(std::vector<std::string> &cmd, Buffer &out) {
    // a dummy `Entry` just for the lookup
    Entry key;
    key.key.swap(cmd[1]);
    key.node.hcode = str_hash((uint8_t *)key.key.data(), key.key.size());
    // hashtable delete
    HNode *node = hm_delete(&g_data.db, &key.node, &entry_eq);
    if (node) { // deallocate the pair
        delete container_of(node, Entry, node);
    }
    return out_int(out, node ? 1 : 0);
}

static bool cb_keys(HNode *node, void *arg) {
    Buffer &out = *(Buffer *)arg;
    const std::string &key = container_of(node, Entry, node)->key;
    out_str(out, key.data(), key.size());
    return true;
}

static void do_keys(std::vector<std::string> &, Buffer &out) {
    out_arr(out, (uint32_t)hm_size(&g_data.db));
    hm_foreach(&g_data.db, &cb_keys, (void *)&out);
}

static void do_request(std::vector<std::string> &cmd, Buffer &out) {
    if (cmd.size() == 2 && cmd[0] == "get") {
        return do_get(cmd, out);
    } else if (cmd.size() == 3 && cmd[0] == "set") {
        return do_set(cmd, out);
    } else if (cmd.size() == 2 && cmd[0] == "del") {
        return do_del(cmd, out);
    } else if (cmd.size() == 1 && cmd[0] == "keys") {
        return do_keys(cmd, out);
    } else {
        return out_err(out, ERR_UNKNOWN, "unknown command.");
    }
}

static void response_begin(Buffer &out, size_t *header) {
    *header = out.size();       // messege header position
    buf_append_u32(out, 0);     // reserve space
}
static size_t response_size(Buffer &out, size_t header) {
    return out.size() - header - 4;
}
static void response_end(Buffer &out, size_t header) {
    size_t msg_size = response_size(out, header);
    if (msg_size > k_max_msg) {
        out.resize(header + 4);
        out_err(out, ERR_TOO_BIG, "response is too big.");
        msg_size = response_size(out, header);
    }
    // message header
    uint32_t len = (uint32_t)msg_size;
    memcpy(&out[header], &len, 4);
}

static void die(const char* message) {
    std::cout << "failed:" << message << std::endl;
}

static void msg(const char* message) {
    std::cout << message << std::endl;
}

static void msg_errno(const char *msg) {
    fprintf(stderr, "[errno:%d] %s\n", errno, msg);
}

static void fd_set_nb(SOCKET fd) {
    unsigned long nonBlockingMode = 1; // 1 for non-blocking, 0 for blocking
    int result = ioctlsocket(fd, FIONBIO, &nonBlockingMode);
    if (result == SOCKET_ERROR) {
        
    }
}

static Conn *handle_accept(SOCKET fd) {
    // accept
    struct sockaddr_in client_addr = {};
    socklen_t addrlen = sizeof(client_addr);
    SOCKET connfd = accept(fd, (struct sockaddr *)&client_addr, &addrlen);
    if (connfd < 0) {
        msg("accept() error"); 
        return NULL; 
    } // error

    uint32_t ip = client_addr.sin_addr.s_addr;
    fprintf(stderr, "new client from %u.%u.%u.%u:%u\n",
        ip & 255, (ip >> 8) & 255, (ip >> 16) & 255, ip >> 24,
        ntohs(client_addr.sin_port)
    );

    // set to non-blocking mode
    fd_set_nb(connfd);
    
    // construct new conn
    Conn *conn = new Conn();
    conn->fd = connfd;
    conn->want_read = true;
    return conn;
}

const size_t k_max_args = 200 * 1000;

static bool read_u32(const uint8_t *&cur, const uint8_t *end, uint32_t &out) {
    if (cur + 4 > end) {
        return false;
    }
    memcpy(&out, cur, 4);
    cur += 4;
    return true;
}

static bool read_str(const uint8_t *&cur, const uint8_t *end, size_t n, std::string &out) {
    if (cur + n > end) {
        return false;
    }
    out.assign(cur, cur + n);
    cur += n;
    return true;
}

// +------+-----+------+-----+------+-----+-----+------+
// | nstr | len | str1 | len | str2 | ... | len | strn |
// +------+-----+------+-----+------+-----+-----+------+

static int32_t parse_req(const uint8_t *data, size_t size, std::vector<std::string> &out) {
    const uint8_t *end = data + size;
    uint32_t nstr = 0;
    if (!read_u32(data, end, nstr)) {
        return -1;
    }

    if (nstr > k_max_args) {
        return -1; // safety limit
    }

    while (out.size() < nstr) {
        uint32_t len = 0;
        if (!read_u32(data, end, len)) {
            return -1;
        }   

        out.push_back(std::string());
        if (!read_str(data, end, len, out.back())) {
            return -1;
        }
    }

    if (data != end) {
        return -1;  // trailing garbage
    }
    return 0;

}

static void make_response(const Response &resp, std::vector<uint8_t> &out) {
    uint32_t resp_len = 4 + (uint32_t)resp.data.size();
    buf_append(out, (const uint8_t *)&resp_len, 4);
    buf_append(out, (const uint8_t *)&resp.status, 4);
    buf_append(out, resp.data.data(), resp.data.size());
}

static bool try_one_request(Conn *conn) {
    if (conn->incoming.size() < 4) {
        return false; // want read, not enough data
    }

    uint32_t len = 0;
    memcpy(&len, conn->incoming.data(), 4);
    if (len > k_max_msg) {  // protocol error
        conn->want_close = true;
        return false;   // want close
    }

    // Protocol message body
    if (4 + len > conn->incoming.size()) {
        return false; // want read, not enough data
    }

    const uint8_t *request = &conn->incoming[4];


    // 4. Process the parsed message
    // got one request, time to do application logic
    std::vector<std::string> cmd;
    if (parse_req(request, len, cmd) < 0) {
        msg("bad request");
        conn->want_close = true;
        return false; // error
    }

    size_t header_pos = 0;
    response_begin(conn->outgoing, &header_pos);
    do_request(cmd, conn->outgoing);
    response_end(conn->outgoing, header_pos);

    
    // application logic done! remove the request message.
    buf_consume(conn->incoming, 4 + len);
    return true;        // success
}

// application callback when the socket is writable
static void handle_write(Conn *conn) {
    assert(conn->outgoing.size() > 0);  
    SSIZE_T rv = send(conn->fd, (char *)conn->outgoing.data(), conn->outgoing.size(), 0);
    if (rv < 0 && errno == EAGAIN) {
        return; // actually not ready
    }
    if (rv < 0) {
        msg("write() error");
        conn->want_close = true;
        return;
    }

    // remove written data from outgoing
    buf_consume(conn->outgoing, (size_t)rv);

    if (conn->outgoing.size() == 0) {
        conn->want_read = true;
        conn->want_write = false;
    }
}

static void handle_read(Conn* conn) {
    // 1. Do a non-blocking read
    uint8_t buf[64 * 1024];
    SSIZE_T rv = recv(conn->fd, (char *)buf, sizeof(buf), 0);
     if (rv < 0 && errno == EAGAIN) {
        return; // actually not ready
    }
    if (rv <= 0) {
        conn->want_close = true;
        return;
    }

    // handle EOF
    if (rv == 0) {
        if (conn->incoming.size() == 0) {
            msg("client closed");
        } else {
            msg("unexpected EOF");
        }
        conn->want_close = true;
        return; // want close
    }

    // 2. Add new data to incoming buffer
    buf_append(conn->incoming, buf, (size_t)rv);

    // 3. Try to parse accumulated buffer
    while (try_one_request(conn)) {}

    if (conn->outgoing.size() > 0) {
        conn->want_read = false;
        conn->want_write = true;
        // The socket is likely ready to write in a request-response protocol,
        // try to write it without waiting for the next iteration.
        handle_write(conn);
    }

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

    fd_set_nb(fd);

    rv = listen(fd, SOMAXCONN);
    if (rv) { die("listen()"); }

    // All client connections, mapped by fd
    std::unordered_map<SOCKET, Conn *> fd2conn;
    std::vector<SOCKET> sockets;
    
    // Event loop
    fd_set readfds;
    fd_set writefds;
    fd_set exceptfds;

    while(true) {
        sockets.clear();
        FD_ZERO(&readfds);
        FD_ZERO(&writefds);
        FD_ZERO(&exceptfds);
        FD_SET(fd, &readfds);
        FD_SET(fd, &exceptfds);
        
        // Put listening sock back first
        sockets.push_back(fd);

        // Handle rest of connection sockets
        for (const auto& connection: fd2conn) {
            Conn* conn = connection.second;

            if (!conn) continue;

            FD_SET(conn->fd, &exceptfds);

            if (conn->want_read) FD_SET(conn->fd, &readfds);
            if (conn->want_write) FD_SET(conn->fd, &writefds);

            sockets.push_back(conn->fd);
        }

        int rv = select(0, &readfds, &writefds, &exceptfds, NULL);

        if (rv == SOCKET_ERROR) {
            die("Poll");
            break;
        }
        if (rv == 0) continue;
        if (rv < 0 && errno == EINTR) continue;
        if (rv < 0) {
            die("Poll");
            break;
        }
        
        if (FD_ISSET(fd, &readfds)) {
            if (Conn *conn = handle_accept(fd)) {
                // Put it into our map
                assert(!fd2conn[conn->fd]);
                fd2conn[conn->fd] = conn;
            }
        }

        for (size_t i = 1; i < sockets.size(); ++i) {
            SOCKET socket = sockets[i];
            Conn *conn = fd2conn[socket];

            if (FD_ISSET(socket, &readfds)) {
                assert(conn->want_read);
                handle_read(conn); // app logic
            }

            if (FD_ISSET(socket, &writefds)) {
                assert(conn->want_write);
                handle_write(conn); // app logic
            }

            if (FD_ISSET(socket, &exceptfds) || conn->want_close) {
                closesocket(conn->fd);
                fd2conn.erase(socket);
                delete conn;
            }

        }
    }
    closesocket(fd);
    WSACleanup();
    return 0;
}