#ifndef FD_SETSIZE
#define FD_SETSIZE 1024 // Define a larger size if needed, before including winsock2.h
#endif
#include <iostream>
#include <cassert>
#include <io.h>
#include <windows.h>
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <winsock2.h>
#include <ws2tcpip.h>
#include <vector>
#include <unordered_map>

#pragma include(lib, "Ws2_32.lib")


int main() { 
    WSADATA wsaData = {0};
    int iResult = 0;

    // Initialize Winsock
    iResult = WSAStartup(MAKEWORD(2,2), &wsaData);
    if (iResult != 0) {
        // Handle error
        printf("WSAStartup failed: %d\n", iResult);
        return 1;
    }  
    WSACleanup();
    return 0;
}