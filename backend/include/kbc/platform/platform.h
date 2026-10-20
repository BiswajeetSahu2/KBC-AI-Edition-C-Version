#ifndef KBC_PLATFORM_H
#define KBC_PLATFORM_H

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET socket_t;
#define CLOSE_SOCKET closesocket
#define BAD_SOCKET INVALID_SOCKET
#else
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
typedef int socket_t;
#define CLOSE_SOCKET close
#define BAD_SOCKET (-1)
#endif

#endif