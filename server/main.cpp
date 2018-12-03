#include <iostream>
#include "../packet/packet.hpp"
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
// #define SERVER_IP "172.18.32.128"
#define SERVER_PORT 8808
using namespace std;
const int timeout = 1000;
void handleGetFile(SOCKET sock, struct sockaddr_in cli_addr, int cli_addr_len, int cli_rwnd, char *filepath);

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

        }
    }
    closesocket(sock);
    WSACleanup();
    return 0;
}

void lget_rdt_rcv(clock_t &clocker, int &base, int &next_seqnum, vector<packet>&packets, SOCKET sock, struct sockaddr_in cli_addr, int cli_addr_len, int &rwnd, int &cwnd, int &ssthresh, bool &stop_rcv);
void lget_time_out(clock_t &clocker, int &base, int &next_seqnum, vector<packet>&packets, SOCKET sock, struct sockaddr_in cli_addr, int cli_addr_len, int &rwnd, int &cwnd, int &ssthresh, bool &stop_timer);

mutex base_mutex;
mutex packets_mutex;
mutex cwnd_mutex;

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
    /* 此线程用于接收客户端的确认包，并更新base,packets,rwnd,cwnd以及ssthresh等 */
    thread rdt_rcv_thread(lget_rdt_rcv, ref(clocker), ref(base), ref(next_seqnum), ref(packets), sock, cli_addr, cli_addr_len, ref(rwnd), ref(cwnd), ref(ssthresh), ref(stop_rcv));

    /* 此线程用于定时器，超时重发 */
    thread time_out_thread(lget_time_out, ref(clocker), ref(base), ref(next_seqnum), ref(packets), sock, cli_addr, cli_addr_len, ref(rwnd), ref(cwnd), ref(ssthresh), ref(stop_timer));
    printf("%d %d %d\n", base, next_seqnum, rwnd);
    /* 主线程用于发送文件 */
    while(true) {
        printf("%d %d %d %d***\n", base, next_seqnum, rwnd, cwnd);
        if (next_seqnum < base + min(rwnd, cwnd)) {
            char buf[DATA_LEN];
            file.read(buf, DATA_LEN);
            if (file.eof()) {
                cout<<"file end."<<endl;
                fin = '1';
            }
            cerr << buf <<endl;
            sndpkt = packet(next_seqnum, 0, 0, '1', "", fin, file.gcount(), buf);
            int sendnum = sendto(sock, (char*)&sndpkt, sizeof(sndpkt), 0, (struct sockaddr*)&cli_addr, cli_addr_len);
            if(sendnum < 0) {
                cerr<<"sendto error"<<endl;
            }
            packets_mutex.lock();
            packets.push_back(sndpkt);
            packets_mutex.unlock();
            if (base == next_seqnum) {
                clocker = clock();
            }
            next_seqnum += 1;
            if (fin == '1') {
                break;
                file.close();
            }
        }
    }
    Sleep(5 * 1000);
    stop_timer = true;
    stop_rcv = true;
    // TerminateThread(&rdt_rcv_thread, 0);
    rdt_rcv_thread.join();
    time_out_thread.join();
    cout << "download finished" <<endl;
}

void lget_rdt_rcv(clock_t &clocker, int &base, int &next_seqnum, vector<packet>&packets, SOCKET sock, struct sockaddr_in cli_addr, int cli_addr_len, int &rwnd, int &cwnd, int &ssthresh, bool &stop_rcv) {
    packet sndpkt;
    int duplicate_ack;
    int ack_count;
    printf("lget_rdt_rcv\n");
    while(true) {
        duplicate_ack = 0;
        ack_count = 0;
        if (stop_rcv) {
            printf("exit the ack pkt receive thread.\n");
            break;
        }
        packet rcvpkt;
        int rcvlen = recvfrom(sock, (char*)&rcvpkt, sizeof(rcvpkt), 0, (struct sockaddr *)&cli_addr, &cli_addr_len);
        if(rcvlen < 0) {
            continue;
        }

        rwnd = rcvpkt.rwnd;
        cout<<"base: "<<base<<endl;
        printf("server receive ack pkt: ack-%d fin-%c\n", rcvpkt.ack, rcvpkt.fin);
        if(base <= rcvpkt.ack) {
            packets_mutex.lock();
                // cwnd慢启动
            if(cwnd < ssthresh) {
                cwnd_mutex.lock();
                cwnd += min(ssthresh - cwnd, rcvpkt.ack - base + 1);
                cwnd_mutex.unlock();
            } else {
                // cwnd拥塞避免
                ack_count += rcvpkt.ack - base + 1;
                if (ack_count >= cwnd) {
                    ack_count = 0;
                    cwnd_mutex.lock();
                    cwnd += 1;
                    cwnd_mutex.unlock();
                }
            }
            auto iter = packets.begin();
            while(iter->seq <= rcvpkt.ack) {
                packets.erase(iter);
                if(packets.size() <= 0) {
                    break;
                }
                iter = packets.begin();
            }
            packets_mutex.unlock();
            base_mutex.lock();
            base = rcvpkt.ack + 1;
            base_mutex.unlock();
            clocker = clock();
            if(rcvpkt.fin == '1') {
                cout << "reveive finished pkt" <<endl;
                sndpkt = packet(next_seqnum, rcvpkt.seq, 0, '1', "", rcvpkt.fin, 0, "");
                int sendnum = sendto(sock, (char*)&sndpkt, sizeof(sndpkt), 0, (struct sockaddr*)&cli_addr, cli_addr_len);
                if(sendnum < 0) {
                    cerr<<"sendto error"<<endl;
                }
                packets.push_back(sndpkt);
                next_seqnum += 1;
            }
        } else {
            duplicate_ack += 1;
            if (duplicate_ack >= 3) {
                cwnd_mutex.lock();
                ssthresh = cwnd / 2;
                cwnd = 1;
                cwnd_mutex.unlock();
            }
        }
    }
}

void lget_time_out(clock_t &clocker, int &base, int &next_seqnum, vector<packet>&packets, SOCKET sock, struct sockaddr_in cli_addr, int cli_addr_len, int &rwnd, int &cwnd, int &ssthresh, bool &stop_timer) {
    printf("lget_time_out\n");
    while(true) {
        if(stop_timer){
            printf("exit the time out thread.\n");
            break;
        }
        if(clock() - clocker > timeout && packets.size() > 0) {
            cwnd_mutex.lock();
            ssthresh = cwnd / 2;
            cwnd = 1;
            cwnd_mutex.unlock();
            base_mutex.lock();
            base = packets.front().seq;
            base_mutex.unlock();
            clocker = clock();
            packets_mutex.lock();
            for(int i=0; i<min(int(packets.size()), min(cwnd, rwnd)); i++) {
                printf("send pkt %d again\n", packets[i].seq);
                int sendnum = sendto(sock, (char*)&packets[i], sizeof(packets[i]), 0, (struct sockaddr*)&cli_addr, cli_addr_len);
                if(sendnum < 0) {
                    cerr<<"sendto error"<<endl;
                }
            }
            packets_mutex.unlock();
        }
    }
}