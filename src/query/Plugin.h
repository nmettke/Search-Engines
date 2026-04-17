#pragma once

#include "../utils/string.hpp"

class PluginObject {
  public:
    // does the pagic path and plugin stuff from linuxtinyserver

    virtual bool MagicPath(const string path) = 0;
    virtual string ProcessRequest(string request) = 0;

    virtual ~PluginObject() {}
};

extern PluginObject *Plugin;
