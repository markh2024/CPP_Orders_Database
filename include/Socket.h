#pragma once
#include <string>
#include <memory>

#ifdef _WIN32
  #include <winsock2.h>
  #include <ws2tcpip.h>
  using SocketHandle = SOCKET;
  constexpr SocketHandle INVALID_SOCK = INVALID_SOCKET;
#else
  using SocketHandle = int;
  constexpr SocketHandle INVALID_SOCK = -1;
#endif

// Thin RAII wrapper around a TCP socket so the rest of the code doesn't
// need to know whether it's compiled for Winsock or POSIX sockets.
class Socket {
public:
    Socket() = default;
    explicit Socket(SocketHandle h, std::string peerAddress = "")
        : handle_(h), peerAddress_(std::move(peerAddress)) {}
    ~Socket();

    Socket(const Socket&) = delete;
    Socket& operator=(const Socket&) = delete;
    Socket(Socket&& other) noexcept;
    Socket& operator=(Socket&& other) noexcept;

    bool valid() const { return handle_ != INVALID_SOCK; }
    void close();

    // Sends the full string, appending "\n" as the message terminator.
    bool sendLine(const std::string& line) const;
    // Blocks until a full "\n"-terminated line is received; returns false on
    // disconnect/error.
    bool recvLine(std::string& out) const;

    SocketHandle raw() const { return handle_; }
    // Only populated on server-side sockets returned by ServerSocket::accept().
    const std::string& peerAddress() const { return peerAddress_; }

private:
    SocketHandle handle_ = INVALID_SOCK;
    std::string peerAddress_;
};

// Must be called once at process start/end on Windows (no-op on Linux).
bool socketLibInit();
void socketLibCleanup();

// Server-side listening socket.
class ServerSocket {
public:
    bool listenOn(int port);
    Socket accept() const;
    void close();
    ~ServerSocket();
private:
    SocketHandle handle_ = INVALID_SOCK;
};

// Client-side connect helper.
std::unique_ptr<Socket> connectTo(const std::string& host, int port);
