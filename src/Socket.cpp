#include "Socket.h"
#include <cstring>
#include <iostream>

#ifdef _WIN32
  #pragma comment(lib, "Ws2_32.lib")
#else
  #include <sys/socket.h>
  #include <netinet/in.h>
  #include <arpa/inet.h>
  #include <unistd.h>
  // A function (not a macro) so it doesn't collide with our own
  // Socket::close()/ServerSocket::close() member functions.
  static inline int closesocket(int fd) { return ::close(fd); }
#endif

bool socketLibInit() {
#ifdef _WIN32
    WSADATA wsaData;
    return WSAStartup(MAKEWORD(2, 2), &wsaData) == 0;
#else
    return true;
#endif
}

void socketLibCleanup() {
#ifdef _WIN32
    WSACleanup();
#endif
}

Socket::~Socket() { close(); }

Socket::Socket(Socket&& other) noexcept
    : handle_(other.handle_), peerAddress_(std::move(other.peerAddress_)) {
    other.handle_ = INVALID_SOCK;
}

Socket& Socket::operator=(Socket&& other) noexcept {
    if (this != &other) {
        close();
        handle_ = other.handle_;
        peerAddress_ = std::move(other.peerAddress_);
        other.handle_ = INVALID_SOCK;
    }
    return *this;
}

void Socket::close() {
    if (handle_ != INVALID_SOCK) {
        closesocket(handle_);
        handle_ = INVALID_SOCK;
    }
}

bool Socket::sendLine(const std::string& line) const {
    std::string msg = line + "\n";
    size_t total = 0;
    while (total < msg.size()) {
        int sent = send(handle_, msg.data() + total,
                         static_cast<int>(msg.size() - total), 0);
        if (sent <= 0) return false;
        total += static_cast<size_t>(sent);
    }
    return true;
}

bool Socket::recvLine(std::string& out) const {
    out.clear();
    char c;
    while (true) {
        int n = recv(handle_, &c, 1, 0);
        if (n <= 0) return false; // disconnected or error
        if (c == '\n') return true;
        if (c != '\r') out += c;
    }
}

bool ServerSocket::listenOn(int port) {
    handle_ = socket(AF_INET, SOCK_STREAM, 0);
    if (handle_ == INVALID_SOCK) return false;

    int opt = 1;
#ifdef _WIN32
    setsockopt(handle_, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));
#else
    setsockopt(handle_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#endif

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(static_cast<uint16_t>(port));

    if (bind(handle_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) return false;
    if (listen(handle_, 16) != 0) return false;
    return true;
}

Socket ServerSocket::accept() const {
    sockaddr_in clientAddr{};
#ifdef _WIN32
    int len = sizeof(clientAddr);
#else
    socklen_t len = sizeof(clientAddr);
#endif
    SocketHandle client = ::accept(handle_, reinterpret_cast<sockaddr*>(&clientAddr), &len);
    if (client == INVALID_SOCK) return Socket(client);

    char ipBuf[INET_ADDRSTRLEN] = {0};
    const char* ipStr = inet_ntop(AF_INET, &clientAddr.sin_addr, ipBuf, sizeof(ipBuf));
    return Socket(client, ipStr ? std::string(ipStr) : std::string("unknown"));
}

void ServerSocket::close() {
    if (handle_ != INVALID_SOCK) {
        closesocket(handle_);
        handle_ = INVALID_SOCK;
    }
}

ServerSocket::~ServerSocket() { close(); }

std::unique_ptr<Socket> connectTo(const std::string& host, int port) {
    SocketHandle h = socket(AF_INET, SOCK_STREAM, 0);
    if (h == INVALID_SOCK) return nullptr;

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(port));
    if (inet_pton(AF_INET, host.c_str(), &addr.sin_addr) != 1) {
        closesocket(h);
        return nullptr;
    }
    if (connect(h, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        closesocket(h);
        return nullptr;
    }
    return std::make_unique<Socket>(h);
}
