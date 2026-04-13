#include "../string.hpp"
#include <iostream>
#include <netdb.h>
#include <openssl/ssl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

const int buffLength = 10240;

// Named distinctly from index/url_features.h::ParsedUrl to avoid ODR/linker
// collisions (same destructor symbol would otherwise delete[] wrong memory).
class SslParsedUrl {
  public:
    const char *CompleteUrl;
    char *Service, *Host, *Port, *Path;

    SslParsedUrl(const char *url);

    ~SslParsedUrl();

  private:
    char *pathBuffer;
};

void initSSL();
void cleanupSSL();
string readURL(string target_url);