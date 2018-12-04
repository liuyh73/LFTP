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
#include <map>
// #define SERVER_IP "172.18.32.128"
const int timeout = 1000;
using namespace std;
class server{
private:
    bool init() {
        //初始化DLL
        WSADATA wsaData;
        WSAStartup(MAKEWORD(2, 2), &wsaData);

        /* sock文件描述符，创建udp套接字  */
        sock = socket(PF_INET, SOCK_DGRAM, 0);
        if(sock < 0) {
            cerr << "sock error"<<endl;
            WSACleanup();
            return false;
        }

        timeval tv = {3000, 0};
        if(setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (char*)&tv, sizeof(timeval))) {
            cerr << "setsockopt failed" <<endl;
            closesocket(sock);
            WSACleanup();
            return false;
        }
        return true;
    }
    
    void handleGetFile(int cli_rwnd, char* filepath) {
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
        int ssthresh = 32;
        char fin = '0';
        bool stop_timer = false;
        bool stop_rcv = false;
        clock_t clocker;
        vector<packet>packets;

        sender snder(base, next_seqnum, rwnd, cwnd, ssthresh, fin, stop_timer, stop_rcv, timeout, filepath, sock, cli_addr, cli_addr_len);
        snder.start();
        printf("%s\n", "download finished");
    }

    void handleSendFile(char *filepath){
        printf("lsend %s\n", filepath);
        packet sndpkt;
        /* check for existence */
        if ((_access(filepath, 0)) != -1) {
            char file_has_existed[] = "The file has been existed.\n";
            sndpkt = packet(0, 0, 0, '0', "", '1', sizeof(file_has_existed), file_has_existed);
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
            sndpkt = packet(0, 0, 0, '0', "", '1', sizeof(file_open_failed), file_open_failed);
            int sendnum = sendto(sock, (char*)&sndpkt, sizeof(sndpkt), 0, (struct sockaddr*)&cli_addr, cli_addr_len);
            if(sendnum < 0) {
                cerr<<"sendto error"<<endl;
            }
            cout<<"Fail to open the file, please try again.\n"<<endl;
            return;
        }

        int bufSize = 64;
        int rwnd = 64;
        int expected_seqnum = 1;
        clock_t clocker;
        bool stop_timer = false;
        queue<packet>pkts_buf;
        sndpkt = packet(0, 0, rwnd, '1', "", '0', 0, "");
        int sendnum = sendto(sock, (char*)&sndpkt, sizeof(sndpkt), 0, (struct sockaddr*)&cli_addr, cli_addr_len);
        if(sendnum < 0) {
            cerr<<"sendto error"<<endl;
        }
        receiver rcver(bufSize, rwnd, expected_seqnum, stop_timer, timeout, filepath, sock, cli_addr, cli_addr_len);
        rcver.start();
        printf("%s\n", "upload finished.");
    }

public:
    SOCKET sock;
    struct sockaddr_in cli_addr;
    int cli_addr_len;
    server(struct sockaddr_in cli_addr, int cli_addr_len){
        if(init()) {
            this->cli_addr.sin_family = cli_addr.sin_family;
            this->cli_addr.sin_addr.s_addr = cli_addr.sin_addr.s_addr;
            this->cli_addr.sin_port = cli_addr.sin_port;
            this->cli_addr_len = cli_addr_len;
        }
    }
    ~server(){
        closesocket(sock);
        WSACleanup();
    }
    void startService(packet rcvpkt) {
        if(string(rcvpkt.cmd) == "lget") {
            handleGetFile(rcvpkt.rwnd, rcvpkt.data);
        } else if (string(rcvpkt.cmd) == "lsend") {
            handleSendFile(rcvpkt.data);
        }
    }
};