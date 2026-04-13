#include <cstring>
#include <iostream>
#include <netdb.h>
#include <openssl/ssl.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

// #include "../string.hpp"
#include "LinuxSSL_Crawler.hpp"

static SSL_CTX *sslCtx = nullptr;

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

    // Connect the socket to the host address.
    int connectResult = connect(socketFD, address->ai_addr, address->ai_addrlen);
    freeaddrinfo(address);
    if (connectResult != 0) {
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

    while ((bytes = SSL_read(ssl, buffer, sizeof(buffer))) > 0) {
        rawResponse.append(buffer, bytes);
    }

    // Close the socket and free the address info structure.

    SSL_shutdown(ssl);
    SSL_free(ssl);
    close(socketFD);

    std::size_t headerEnd = rawResponse.find("\r\n\r\n");
    if (headerEnd == string::npos) {
        return rawResponse;
    }

    return rawResponse.substr(headerEnd + 4);
}