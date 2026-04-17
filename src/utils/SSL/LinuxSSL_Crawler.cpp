#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <netdb.h>
#include <openssl/ssl.h>
#include <signal.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

// #include "../string.hpp"
#include "LinuxSSL_Crawler.hpp"

static SSL_CTX *sslCtx = nullptr;

// Hard cap on a single HTTP(S) response body to prevent a malicious or
// misbehaving server from OOMing the crawler by streaming unbounded data.
// 10 MB easily covers normal HTML pages and robots.txt files.
static constexpr size_t maxResponseBytes = 10ULL * 1024 * 1024;

static bool asciiIsDigit(char c) { return c >= '0' && c <= '9'; }

// Parses the numeric status from the first line of an HTTP response, e.g.
// "HTTP/1.1 200 OK". Returns -1 if the line cannot be interpreted.
static int parseHttpStatusCode(const string &rawResponse) {
    size_t lineEnd = rawResponse.find("\r\n");
    if (lineEnd == string::npos) {
        lineEnd = rawResponse.find('\n');
    }
    if (lineEnd == string::npos || lineEnd == 0) {
        return -1;
    }

    string statusLine = rawResponse.substr(0, lineEnd);
    if (statusLine.size() < 12 || statusLine.find("HTTP/") != 0) {
        return -1;
    }

    size_t i = statusLine.find(' ');
    if (i == string::npos) {
        return -1;
    }
    while (i < statusLine.size() && statusLine[i] == ' ') {
        ++i;
    }

    int code = 0;
    size_t digits = 0;
    for (; i < statusLine.size(); ++i) {
        const char ch = statusLine[i];
        if (!asciiIsDigit(ch)) {
            break;
        }
        code = code * 10 + (ch - '0');
        ++digits;
        if (digits > 3) {
            return -1;
        }
    }
    if (digits == 0) {
        return -1;
    }
    return code;
}

static string decodeHttpResponseBodyIfSuccessful(const string &rawResponse) {
    size_t headerEnd = rawResponse.find("\r\n\r\n");
    size_t bodySkip = 4;
    if (headerEnd == string::npos) {
        headerEnd = rawResponse.find("\n\n");
        bodySkip = 2;
    }
    if (headerEnd == string::npos) {
        return "";
    }

    const int status = parseHttpStatusCode(rawResponse);
    if (status < 200 || status > 299) {
        return "";
    }

    return rawResponse.substr(headerEnd + bodySkip);
}

// Bound TCP connect() so an unreachable host doesn't pin a crawler thread
// for the kernel default (~75s). SO_RCVTIMEO/SO_SNDTIMEO do not affect connect.
static constexpr int connectTimeoutSecs = 10;

// Non-blocking connect with a bounded timeout. Returns true iff the socket is
// fully connected. On return, the socket is restored to blocking mode so the
// caller can use ordinary send()/recv() (and SSL_connect, which expects a
// blocking-mode fd) with the socket-level timeouts already set.
// Caller owns fd in all cases (this helper never closes it).
static bool connectWithTimeout(int fd, const sockaddr *addr, socklen_t addrlen, int timeoutSecs) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) {
        return false;
    }
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        return false;
    }

    int ret = connect(fd, addr, addrlen);
    if (ret == 0) {
        fcntl(fd, F_SETFL, flags);
        return true;
    }
    if (errno != EINPROGRESS) {
        return false;
    }

    fd_set writeSet;
    FD_ZERO(&writeSet);
    FD_SET(fd, &writeSet);

    timeval timeout{};
    timeout.tv_sec = timeoutSecs;
    timeout.tv_usec = 0;

    int selectResult = select(fd + 1, nullptr, &writeSet, nullptr, &timeout);
    if (selectResult <= 0) {
        return false;
    }

    int sockErr = 0;
    socklen_t len = sizeof(sockErr);
    if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &sockErr, &len) < 0 || sockErr != 0) {
        return false;
    }

    fcntl(fd, F_SETFL, flags);
    return true;
}

void initSSL() {
    signal(SIGPIPE, SIG_IGN);
    SSL_library_init();
    sslCtx = SSL_CTX_new(SSLv23_method());
}

void cleanupSSL() {
    if (sslCtx) {
        SSL_CTX_free(sslCtx);
        sslCtx = nullptr;
    }
}

SslParsedUrl::SslParsedUrl(const char *url) {
    // Assumes url points to static text but
    // does not check.

    CompleteUrl = url;

    pathBuffer = new char[strlen(url) + 1];
    const char *f;
    char *t;
    for (t = pathBuffer, f = url; *t++ = *f++;)
        ;

    Service = pathBuffer;

    const char Colon = ':', Slash = '/';
    char *p;
    for (p = pathBuffer; *p && *p != Colon; p++)
        ;

    if (*p) {
        // Mark the end of the Service.
        *p++ = 0;

        if (*p == Slash)
            p++;
        if (*p == Slash)
            p++;

        Host = p;

        for (; *p && *p != Slash && *p != Colon; p++)
            ;

        if (*p == Colon) {
            // Port specified.  Skip over the colon and
            // the port number.
            *p++ = 0;
            Port = +p;
            for (; *p && *p != Slash; p++)
                ;
        } else
            Port = p;

        if (*p)
            // Mark the end of the Host and Port.
            *p++ = 0;

        // Whatever remains is the Path.
        Path = p;
    } else
        Host = Path = p;
}

SslParsedUrl::~SslParsedUrl() { delete[] pathBuffer; }

string readURL(string target_url) {
    if (!sslCtx) {
        return "";
    }

    // Parse the URL.
    SslParsedUrl url(target_url.cstr());
    if (!url.Host || !*url.Host) {
        return "";
    }

    // Get the host address.
    struct addrinfo *address = nullptr;
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    int gaiResult = getaddrinfo(url.Host, "443", &hints, &address);
    if (gaiResult != 0 || !address) {
        return "";
    }

    // Create a TCP/IP socket.
    int socketFD = socket(address->ai_family, address->ai_socktype, address->ai_protocol);
    if (socketFD < 0) {
        freeaddrinfo(address);
        return "";
    }

    timeval timeout;
    timeout.tv_sec = 10;
    timeout.tv_usec = 0;
    setsockopt(socketFD, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(socketFD, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));

    // Connect the socket to the host address with a bounded timeout so
    // unreachable hosts don't pin this thread + fd for ~75s (kernel default).
    bool connected =
        connectWithTimeout(socketFD, address->ai_addr, address->ai_addrlen, connectTimeoutSecs);
    freeaddrinfo(address);
    if (!connected) {
        close(socketFD);
        return "";
    }

    // Build an SSL layer and set it to read/write
    // to the socket we've connected.

    SSL *ssl = SSL_new(sslCtx);
    if (!ssl) {
        close(socketFD);
        return "";
    }

    SSL_set_tlsext_host_name(ssl, url.Host);
    if (SSL_set_fd(ssl, socketFD) != 1) {
        SSL_free(ssl);
        close(socketFD);
        return "";
    }

    if (SSL_connect(ssl) != 1) {
        SSL_shutdown(ssl);
        SSL_free(ssl);
        close(socketFD);
        return "";
    }

    // Send a GET message.

    string path = "/";
    if (url.Path && *url.Path)
        path += url.Path;

    string getMessage = "GET " + path + " HTTP/1.1\r\nHost:" + string(url.Host) +
                        "\r\nUser-Agent: LinuxGetSsl/2.0 mjjiang@umich.edu (Linux)\r\nAccept: */* "
                        "\r\nAccept-Encoding: identity\r\nConnection: close\r\n\r\n";
    if (SSL_write(ssl, getMessage.cstr(), getMessage.size()) <= 0) {
        SSL_shutdown(ssl);
        SSL_free(ssl);
        close(socketFD);
        return "";
    }

    // Read from the socket until there's no more data, copying it to
    // stdout.
    char buffer[buffLength];
    int bytes;
    string rawResponse = "";
    bool responseTooLarge = false;

    while ((bytes = SSL_read(ssl, buffer, sizeof(buffer))) > 0) {
        rawResponse.append(buffer, bytes);
        if (rawResponse.size() > maxResponseBytes) {
            responseTooLarge = true;
            break;
        }
    }

    // Close the socket and free the address info structure.

    SSL_shutdown(ssl);
    SSL_free(ssl);
    close(socketFD);

    // Drop oversize responses entirely rather than feeding a truncated body
    // into the HTML parser (unbalanced tags would pollute the index).
    if (responseTooLarge) {
        return "";
    }

    return decodeHttpResponseBodyIfSuccessful(rawResponse);
}