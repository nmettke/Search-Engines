
#include "Plugin.h"
#include "SearchCommon.h"

// SearchPlugin only handles the / -> /index.html redirect

class SearchPlugin : public PluginObject {
  public:
    bool MagicPath(const string path) override { return path == "/"; }

    string ProcessRequest(string /* req */) override {
        // redirect "/" to "/index.html"
        return "HTTP/1.1 302 Found\r\n"
               "Location: /index.html\r\n"
               "Connection: close\r\n\r\n";
    }

    SearchPlugin() { Plugin = this; }
};

static SearchPlugin theSearchPlugin;
