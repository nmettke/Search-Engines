#include "unix_socket.h"

#include <cstddef>
#include <cstring>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

int ipcUnixConnect(const char *path) {
    if (!path || !*path)
        return -1;
    int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0)
        return -1;
    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);
    socklen_t addrlen = static_cast<socklen_t>(offsetof(sockaddr_un, sun_path)) +
                        static_cast<socklen_t>(std::strlen(addr.sun_path)) + 1u;
    if (::connect(fd, reinterpret_cast<sockaddr *>(&addr), addrlen) < 0) {
        ::close(fd);
        return -1;
    }
    return fd;
}

int ipcUnixListen(const char *path) {
    if (!path || !*path)
        return -1;
    ::unlink(path);
    int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0)
        return -1;
    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);
    socklen_t addrlen = static_cast<socklen_t>(offsetof(sockaddr_un, sun_path)) +
                        static_cast<socklen_t>(std::strlen(addr.sun_path)) + 1u;
    if (::bind(fd, reinterpret_cast<sockaddr *>(&addr), addrlen) < 0) {
        ::close(fd);
        return -1;
    }
    if (::listen(fd, 4) < 0) {
        ::close(fd);
        return -1;
    }
    return fd;
}
