#include <iostream>
#include "../packet/packet.hpp"
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
void handleGetFile(SOCKET sock, struct sockaddr_in svc_addr, int svc_addr_len);
void handleSendFile(SOCKET sock, struct sockaddr_in svc_addr, int svc_addr_len);
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
        handleGetFile(sock, svc_addr, svc_addr_len);
    } else if (string(cmd) == "lsend") {
        handleSendFile(sock, svc_addr, svc_addr_len);
    }
    closesocket(sock);
    WSACleanup();
    return 0;
}

mutex pkts_buf_mutex;
mutex rwnd_mutex;
void readFromBuf(ofstream &file, queue<packet>&pkts_buf, int &bufSize, int &rwnd);

void handleGetFile(SOCKET sock, struct sockaddr_in svc_addr, int svc_addr_len) {
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

    thread read_from_buf_thread(readFromBuf, ref(file), ref(pkts_buf), ref(bufSize), ref(rwnd));
    
    while(true) {
        printf("wait for rcv\n");
        int rcvlen = recvfrom(sock, (char*)&rcvpkt, sizeof(rcvpkt), 0, (struct sockaddr *)&svc_addr, &svc_addr_len);
        printf("%d %d %d %c %c %d\n", rcvpkt.seq, rcvpkt.ack, rcvpkt.rwnd, rcvpkt.status, rcvpkt.fin, rcvpkt.len);
        if(rcvlen < 0) {
            continue;
        }
        cerr <<rcvpkt.seq << " " << expected_seqnum <<" "<<rwnd <<endl;
        if (rcvpkt.seq == expected_seqnum) {
            if (rcvpkt.status == '0') {
                printf("%s\n", rcvpkt.data);
                try{
                    TerminateThread(&read_from_buf_thread, 0);
                } catch(exception e) {
                    
                }
                return;
            }
            if (rwnd > 1) {
                rwnd_mutex.lock();
                rwnd -= 1;
                rwnd_mutex.unlock();
                expected_seqnum += 1;
                sndpkt = packet(expected_seqnum, rcvpkt.seq, rwnd, 1, "", rcvpkt.fin, 0, "");
                printf("receive pkt %d.\n", rcvpkt.seq);
                pkts_buf_mutex.lock();
                pkts_buf.push(rcvpkt);
                pkts_buf_mutex.unlock();
            }
        }

        printf("send ack pkt %d.\n", sndpkt.ack);
        int sndlen = sendto(sock, (char*)&sndpkt, sizeof(sndpkt), 0, (struct sockaddr *)&svc_addr, svc_addr_len);
        if(sndlen < 0) {
            cerr << "sendto error"<<endl;
        }

        if (sndpkt.fin == '1') {
            clocker = clock();
            cout<< "break" <<endl;
            break;
        }
    }

    thread lget_fin_timer([&]{
        while(true) {
            if (stop_timer) break;
            if(clock() - clocker > timeout) {
                clocker = clock();
                int sndlen = sendto(sock, (char*)&sndpkt, sizeof(sndpkt), 0, (struct sockaddr *)&svc_addr, svc_addr_len);
                if(sndlen < 0) {
                    cerr << "sendto error"<<endl;
                }
            }
        }
    });

    while(true) {
        int rcvlen = recvfrom(sock, (char*)&rcvpkt, sizeof(rcvpkt), 0, (struct sockaddr *)&svc_addr, &svc_addr_len);
        printf("%d %d %d %c %c %d\n", rcvpkt.seq, rcvpkt.ack, rcvpkt.rwnd, rcvpkt.status, rcvpkt.fin, rcvpkt.len);
        if(rcvlen < 0) {
            continue;
        }
        cout << "rcvpkt.seq: "<<rcvpkt.seq<< " fin pkt: " << rcvpkt.fin << endl;
        if(rcvpkt.seq == expected_seqnum && rcvpkt.fin == '1') {
            cout<<"succeed to receive fin pkt."<<endl;
            break;
        }
    }
    file.close();
    stop_timer = true;
    lget_fin_timer.join();
    read_from_buf_thread.join();
    printf("%s", "download finished.");
}

void readFromBuf(ofstream &file, queue<packet>&pkts_buf, int &bufSize, int &rwnd) {
    while(true) {
        // Sleep(5);
        // 生成随机数，读取n个包
        int count = rand()%(bufSize - rwnd + 1) + 5;
        pkts_buf_mutex.lock();
        while(count > 0 && !pkts_buf.empty()) {
            packet pkt = pkts_buf.front();
            pkts_buf.pop();
            count -= 1;
            rwnd_mutex.lock();
            rwnd += 1;
            rwnd_mutex.unlock();
            file.write(pkt.data, pkt.len);
            if (pkt.fin == '1') {
                return;
            }
        }
        pkts_buf_mutex.unlock();
    }
}

void handleSendFile(SOCKET sock, struct sockaddr_in svc_addr, int svc_addr_len) {
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
            rwnd = rcvpkt.rwnd;
            break;
        }
    }

    /* 此线程用于接收客户端的确认包，并更新base,packets,rwnd,cwnd以及ssthresh等 */
    


}