#include <netdb.h>
#include <openssl/ssl.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <iostream>

class ParsedUrl {
  public:
    const char *CompleteUrl;
    char *Service, *Host, *Port, *Path;

    ParsedUrl(const char *url) {
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

    ~ParsedUrl() { delete[] pathBuffer; }

  private:
    char *pathBuffer;
};

int main(int argc, char **argv) {

    if (argc != 2) {
        std::cerr << "Usage: " << argv[0] << " url" << std::endl;
        return 1;
    }

    // Parse the URL
    ParsedUrl url(argv[1]);

    // Get the host address.
    struct addrinfo *address, hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    getaddrinfo(url.Host, "443", &hints, &address);

    // Create a TCP/IP socket.
    int socketFD = socket(hints.ai_family, hints.ai_socktype, hints.ai_protocol);

    // Connect the socket to the host address.
    int connectResult = connect(socketFD, address->ai_addr, address->ai_addrlen);

    // Build an SSL layer and set it to read/write
    // to the socket we've connected.

    SSL_library_init();
    SSL_CTX *ctx = SSL_CTX_new(SSLv23_method());
    SSL *ssl = SSL_new(ctx);
    SSL_set_tlsext_host_name(ssl, url.Host);
    SSL_set_fd(ssl, socketFD);
    SSL_connect(ssl);

    // Send a GET message.

    std::string path = "/";
    if (url.Path && *url.Path)
        path += url.Path;

    std::string getMessage = "GET " + path + " HTTP/1.1\r\nHost:" + std::string(url.Host) +
                             "\r\nUser-Agent: LinuxGetSsl/2.0 mettke@umich.edu (Linux)\r\nAccept: "
                             "*/* \r\nAccept-Encoding: identity\r\nConnection: close\r\n\r\n";
    SSL_write(ssl, getMessage.c_str(), getMessage.length());

    // Read from the socket until there's no more data, copying it to
    // stdout.
    char buffer[10240];
    int bytes;
    bool content = false;

    while ((bytes = SSL_read(ssl, buffer, sizeof(buffer))) > 0) {
        if (!content) {
            size_t end = std::string(buffer).find("\r\n\r\n");
            if (end != std::string::npos) {
                content = true;
                // end + 4 is the location of the first char in content
                write(1, buffer + end + 4, bytes - end - 4);
            }
        } else {
            write(1, buffer, bytes);
        }
    }

    // Close the socket and free the address info structure.

    SSL_shutdown(ssl);
    SSL_free(ssl);
    close(socketFD);
    SSL_CTX_free(ctx);
}
