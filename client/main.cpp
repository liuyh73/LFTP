#include <iostream>
#include "../role/receiver.cpp"
#include "../packet/packet.hpp"
#include "../role/sender.cpp"
#include <stdio.h>
#include <cstdlib>
#include <winsock2.h>
#include <cstring>
#include <fstream>
#include <thread>
#include <io.h>
#include <ctime>
#include <queue>
#include <mutex>
// #include <Mmsystem.h>             //timeGetTime()  
// #pragma comment(lib, "Winmm.lib")   //timeGetTime() 
char cmd[6];
char ip[15];
const int port = 8808;
const int timeout = 1000;
char filepath[100];
using namespace std;
void getFile(SOCKET sock, struct sockaddr_in svc_addr, int svc_addr_len);
void sendFile(SOCKET sock, struct sockaddr_in svc_addr, int svc_addr_len);
int main(int argc, char *argv[]) {
    srand(time(nullptr));
    strcpy(cmd, argv[1]);
    strcpy(ip, argv[2]);
    strcpy(filepath, argv[3]);
    printf("%s, %s, %s\n", cmd, ip, filepath);
    //初始化DLL
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);

    /* sock文件描述符，创建udp套接字  */
    SOCKET sock = socket(PF_INET, SOCK_DGRAM, 0);
    if(sock < 0) {
        cerr << "sock error"<<endl;
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

    /* 设置address */
    struct sockaddr_in svc_addr;
    memset(&svc_addr, 0, sizeof(svc_addr));
    svc_addr.sin_family = AF_INET;
    svc_addr.sin_addr.s_addr = inet_addr(ip);
    svc_addr.sin_port = port;
    int svc_addr_len = sizeof(svc_addr);

    struct packet sndpkt;
    if(string(cmd) == "lget") {
        getFile(sock, svc_addr, svc_addr_len);
    } else if (string(cmd) == "lsend") {
        sendFile(sock, svc_addr, svc_addr_len);
    }
    closesocket(sock);
    WSACleanup();
    return 0;
}

void getFile(SOCKET sock, struct sockaddr_in svc_addr, int svc_addr_len) {
    /* check for existence */
    // if ((_access(filepath, 0)) != -1) {
    //     printf("The file has been existed.");
    //     return;
    // }
    
    int bufSize = 128;
    int rwnd = 128;
    int expected_seqnum = 1;
    clock_t clocker;
    bool stop_timer = false;
    queue<packet>pkts_buf;
    packet rcvpkt;
    packet sndpkt = packet(expected_seqnum, 0, rwnd, 1, cmd, '0', sizeof(filepath), filepath);
    int sndlen = sendto(sock, (char*)&sndpkt, sizeof(sndpkt), 0, (struct sockaddr *)&svc_addr, svc_addr_len);
    if(sndlen < 0) {
        cerr << "sendto error"<<endl;
    }
    /* 以写、二进制方式打开文件 */
    ofstream file(filepath, ios::out|ios::binary);
    if(!file.is_open()) {
        printf("Fail to create the file, please try again.");
        return;
    }
    receiver rcver(bufSize, rwnd, expected_seqnum, stop_timer, timeout, filepath, sock, svc_addr, svc_addr_len);
    rcver.start();
}

void sendFile(SOCKET sock, struct sockaddr_in svc_addr, int svc_addr_len) {
    /* check for existence */
    if ((_access(filepath, 0)) == -1) {
        printf("The file doesn't exist.");
        return;
    }

    /* 以读、二进制方式打开文件 */
    ofstream file(filepath, ios::in|ios::binary);
    if(!file.is_open()) {
        printf("Fail to create the file, please try again.");
        return;
    }

    int base = 1;
    int next_seqnum = 1;
    int rwnd = 0;
    int cwnd = 1;
    int ssthresh = 64;
    char fin = '0';
    bool stop_timer = false;
    bool stop_rcv = false;
    clock_t clocker;
    vector<packet>packets;
    packet rcvpkt;
    packet sndpkt = packet(0, 0, 0, 1, cmd, '0', sizeof(filepath), filepath);
    int sndlen, rcvlen;
    // 发送lsend命令包，并获得服务器回传数据。
    while(true) {
        sndlen = sendto(sock, (char*)&sndpkt, sizeof(sndpkt), 0, (struct sockaddr *)&svc_addr, svc_addr_len);
        if(sndlen < 0) {
            continue;
        }
        rcvlen = recvfrom(sock, (char*)&rcvpkt, sizeof(rcvpkt), 0, (struct sockaddr *)&svc_addr, &svc_addr_len);
        if(rcvlen > 0) {
            if(rcvpkt.status == '0') {
                printf("%s\n", rcvpkt.data);
                return;
            }
            rwnd = rcvpkt.rwnd;
            break;
        }
    }

    sender snder(base, next_seqnum, rwnd, cwnd, ssthresh, fin, stop_timer, stop_rcv, timeout, filepath, sock, svc_addr, svc_addr_len);
    snder.start();
}