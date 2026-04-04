#pragma once

// Returns connected stream socket fd, or -1 on failure.
int ipcUnixConnect(const char *path);

// Creates AF_UNIX listening socket; unlinks path first. Returns fd or -1.
int ipcUnixListen(const char *path);
