#include <iostream>
#include "../role/sender.cpp"
#include "../packet/packet.hpp"
#include "../role/receiver.cpp"
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
// #define SERVER_IP "172.18.32.128"
#define SERVER_PORT 8808
using namespace std;
const int timeout = 1000;
void handleGetFile(SOCKET sock, struct sockaddr_in cli_addr, int cli_addr_len, int cli_rwnd, char *filepath);
void handleSendFile(SOCKET sock, struct sockaddr_in cli_addr, int cli_addr_len, char *filepath);
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
    
    struct sockaddr_in cli_addr;
    struct packet rcvpkt;
    int cli_addr_len = sizeof(cli_addr);
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

    while(true) {
        cout<<"waiting for connection..."<<endl;
        recvfrom(sock, (char*)&rcvpkt, sizeof(rcvpkt), 0, (struct sockaddr *)&cli_addr, &cli_addr_len);
        if(string(rcvpkt.cmd) == "lget") {
            thread lgetFile(handleGetFile, sock, cli_addr, cli_addr_len, rcvpkt.rwnd, rcvpkt.data);
            lgetFile.detach();
        } else if (string(rcvpkt.cmd) == "lsend") {
            thread lsendFile(handleSendFile, sock, cli_addr, cli_addr_len, rcvpkt.data);
            lsendFile.detach();
        }
    }
    closesocket(sock);
    WSACleanup();
    return 0;
}

void handleGetFile(SOCKET sock, struct sockaddr_in cli_addr, int cli_addr_len, int cli_rwnd, char* filepath) {
    printf("lget %s\n", filepath);
    packet sndpkt;
    /* check for existence */
    if ((_access(filepath, 0)) == -1) {
        char file_not_exists[] = "The file doesn't not exist.\n";
        sndpkt = packet(1, 0, 0, '0', "", 1, sizeof(file_not_exists), file_not_exists);
        int sendnum = sendto(sock, (char*)&sndpkt, sizeof(sndpkt), 0, (struct sockaddr*)&cli_addr, cli_addr_len);
        if(sendnum < 0) {
            cerr<<"sendto error"<<endl;
        }
        cout<<"The file doesn't not exist."<<endl;
        return;
    }
    /* 以读、二进制方式打开文件 */
    ifstream file(filepath, ios::in|ios::binary);
    if(!file.is_open()) {
        char file_open_failed[] = "Fail to open the file, please try again.\n";
        sndpkt = packet(1, 0, 0, '0', "", 1, sizeof(file_open_failed), file_open_failed);
        int sendnum = sendto(sock, (char*)&sndpkt, sizeof(sndpkt), 0, (struct sockaddr*)&cli_addr, cli_addr_len);
        if(sendnum < 0) {
            cerr<<"sendto error"<<endl;
        }
        cout<<"Fail to open the file, please try again.\n"<<endl;
        return;
    }
    int base = 1;
    int next_seqnum = 1;
    int rwnd = cli_rwnd;
    int cwnd = 1;
    int ssthresh = 64;
    char fin = '0';
    bool stop_timer = false;
    bool stop_rcv = false;
    clock_t clocker;
    vector<packet>packets;

    sender snder(base, next_seqnum, rwnd, cwnd, ssthresh, fin, stop_timer, stop_rcv, timeout, filepath, sock, cli_addr, cli_addr_len);
    snder.start();
}

void handleSendFile(SOCKET sock, struct sockaddr_in cli_addr, int cli_addr_len, char *filepath){
    printf("lsend %s\n", filepath);
    packet sndpkt;
    /* check for existence */
    if ((_access(filepath, 0)) != -1) {
        char file_has_existed[] = "The file has been existed.\n";
        sndpkt = packet(0, 0, 0, '0', "", 1, sizeof(file_has_existed), file_has_existed);
        int sendnum = sendto(sock, (char*)&sndpkt, sizeof(sndpkt), 0, (struct sockaddr*)&cli_addr, cli_addr_len);
        if(sendnum < 0) {
            cerr<<"sendto error"<<endl;
        }
        cout<<file_has_existed<<endl;
        return;
    }

    /* 以写、二进制方式打开文件 */
    ofstream file(filepath, ios::out|ios::binary);
    if(!file.is_open()) {
        char file_open_failed[] = "Fail to open the file, please try again.\n";
        sndpkt = packet(0, 0, 0, '0', "", 1, sizeof(file_open_failed), file_open_failed);
        int sendnum = sendto(sock, (char*)&sndpkt, sizeof(sndpkt), 0, (struct sockaddr*)&cli_addr, cli_addr_len);
        if(sendnum < 0) {
            cerr<<"sendto error"<<endl;
        }
        cout<<"Fail to open the file, please try again.\n"<<endl;
        return;
    }

    int bufSize = 128;
    int rwnd = 128;
    int expected_seqnum = 1;
    clock_t clocker;
    bool stop_timer = false;
    queue<packet>pkts_buf;
    sndpkt = packet(0, 0, rwnd, '1', "", 1, 0, 0);
    int sendnum = sendto(sock, (char*)&sndpkt, sizeof(sndpkt), 0, (struct sockaddr*)&cli_addr, cli_addr_len);
    if(sendnum < 0) {
        cerr<<"sendto error"<<endl;
    }
    receiver rcver(bufSize, rwnd, expected_seqnum, stop_timer, timeout, filepath, sock, cli_addr, cli_addr_len);
    rcver.start();
}
