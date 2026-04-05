#include <cstring>
#include <iostream>
#include <netdb.h>
#include <openssl/ssl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

// #include "../string.hpp"
#include "LinuxSSL_Crawler.hpp"

ParsedUrl::ParsedUrl(const char *url) {
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

ParsedUrl::~ParsedUrl() { delete[] pathBuffer; }

string readURL(string target_url) {
    // Parse the URL
    ParsedUrl url(target_url.cstr());

    struct addrinfo hints {};
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    struct addrinfo *address = nullptr;
    if (getaddrinfo(url.Host, "443", &hints, &address) != 0 || address == nullptr) {
        return string("");
    }

    int socketFD = socket(address->ai_family, address->ai_socktype, address->ai_protocol);
    if (socketFD < 0) {
        freeaddrinfo(address);
        return string("");
    }

    if (connect(socketFD, address->ai_addr, address->ai_addrlen) != 0) {
        close(socketFD);
        freeaddrinfo(address);
        return string("");
    }

    freeaddrinfo(address);

    SSL_library_init();
    SSL_CTX *ctx = SSL_CTX_new(SSLv23_method());
    if (!ctx) {
        close(socketFD);
        return string("");
    }
    SSL *ssl = SSL_new(ctx);
    if (!ssl) {
        SSL_CTX_free(ctx);
        close(socketFD);
        return string("");
    }
    SSL_set_tlsext_host_name(ssl, url.Host);
    SSL_set_fd(ssl, socketFD);
    if (SSL_connect(ssl) != 1) {
        SSL_free(ssl);
        SSL_CTX_free(ctx);
        close(socketFD);
        return string("");
    }

    // Send a GET message.

    string path = "/";
    if (url.Path && *url.Path)
        path += url.Path;

    string getMessage = "GET " + path + " HTTP/1.1\r\nHost:" + string(url.Host) +
                        "\r\nUser-Agent: LinuxGetSsl/2.0 mettke@umich.edu (Linux)\r\nAccept: */* "
                        "\r\nAccept-Encoding: identity\r\nConnection: close\r\n\r\n";
    SSL_write(ssl, getMessage.cstr(), getMessage.size());

    // Read from the socket until there's no more data.
    // IMPORTANT: SSL_read does not null-terminate. Never use string(buffer) — that scans for '\0'
    // and is undefined behavior / can segfault past the end of the stack buffer.
    char buffer[buffLength];
    int bytes = 0;
    bool content = false;

    string returnVal = "";

    while ((bytes = SSL_read(ssl, buffer, sizeof(buffer))) > 0) {
        if (!content) {
            string chunk(buffer, buffer + bytes);
            size_t end = chunk.find("\r\n\r\n");
            if (end != string::npos) {
                content = true;
                returnVal.append(chunk.cstr() + end + 4, chunk.size() - end - 4);
            }
        } else {
            returnVal.append(buffer, static_cast<size_t>(bytes));
        }
    }

    SSL_shutdown(ssl);
    SSL_free(ssl);
    close(socketFD);
    SSL_CTX_free(ctx);
    return returnVal;
}
