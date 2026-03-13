#include <netdb.h>
#include <openssl/ssl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <iostream>

const int buffLength = 10240;

class ParsedUrl {
  public:
    const char *CompleteUrl;
    char *Service, *Host, *Port, *Path;

    ParsedUrl(const char *url);

    ~ParsedUrl();

  private:
    char *pathBuffer;
};

std::string readURL(std::string target_url);