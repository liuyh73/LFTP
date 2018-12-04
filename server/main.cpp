#include <iostream>
#include "../packet/packet.hpp"
#include "server.cpp"
#include <cstdio>
#include <cstdlib>
#include <winsock2.h>
#include <io.h>
#include <cstring>
#include <string>
#include <thread>
#include <fstream>
#include <windows.h> 
#include <vector>
#include <mutex>
#include <cmath>
#include <queue>
#include <map>
using namespace std;
#define SERVER_PORT 8808
void startService(packet rcvpkt, SOCKET sock, struct sockaddr_in cli_addr, int cli_addr_len);
int main() {
    //初始化DLL
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
    /* sock --- socket文件描述符，创建udp套接字 */
    SOCKET sock = socket(PF_INET, SOCK_DGRAM, 0);
    if(sock < 0) {
        cerr<<"sock error"<<endl;
        WSACleanup();
        exit(1);
    }
    /* 将服务端套接字和IP、端口号绑定 */
    struct sockaddr_in svc_addr;
    memset(&svc_addr, 0, sizeof(svc_addr));  //每个字节都用0填充
    svc_addr.sin_family = PF_INET;   //使用IPv4地址
    // svc_addr.sin_addr.s_addr = inet_addr(SERVER_IP);  //具体的IP地址
    svc_addr.sin_port = SERVER_PORT;

    /* INADDR_ANY表示不管哪个网卡接收到数据，只要目的端口是SERVER_PORT，就会被该应用程序接收到 */
    svc_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    /* 绑定socket */
    if(bind(sock, (struct sockaddr *)&svc_addr, sizeof(svc_addr)) < 0) {
        cerr<<"bind error"<<endl;
        closesocket(sock);
        WSACleanup();
        exit(1);
    }

    timeval tv = {3000, 0};
    if(setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (char*)&tv, sizeof(timeval))) {
        cerr << "setsockopt failed" <<endl;
        closesocket(sock);
        WSACleanup();
        exit(1);
    }
    struct packet rcvpkt;
    struct sockaddr_in cli_addr;
    int cli_addr_len = sizeof(cli_addr);

    while(true) {
        cout<<"waiting for connection..."<<endl;
        if(recvfrom(sock, (char*)&rcvpkt, sizeof(rcvpkt), 0, (struct sockaddr *)&cli_addr, &cli_addr_len) < 0){
            continue;
        }
        cout << cli_addr.sin_addr.S_un.S_addr << " 444 " << cli_addr_len <<endl;
        thread serveToClient(startService, rcvpkt, sock, cli_addr, cli_addr_len);
        serveToClient.detach();
    }
    return 0;
}

void startService(packet rcvpkt, SOCKET sock, struct sockaddr_in cli_addr, int cli_addr_len) {
    server *svr = new server(cli_addr, cli_addr_len);
    cout << svr->cli_addr.sin_addr.S_un.S_addr << " 222 " << svr->cli_addr_len <<endl;
    packet sndpkt(0, 0, 0, '1', rcvpkt.cmd, '0', 0, "");
    int sendnum = sendto(svr->sock, (char*)&sndpkt, sizeof(sndpkt), 0, (struct sockaddr*)&(svr->cli_addr), svr->cli_addr_len);
    if(sendnum < 0) {
        cerr<<"sendto error"<<endl;
    }
    svr->startService(rcvpkt);
    delete svr;
}