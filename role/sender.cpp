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
using namespace std;
class sender {
private:
    int base;
    int next_seqnum;
    int rwnd;
    int cwnd;
    int ssthresh;
    char fin;
    bool stop_timer;
    bool stop_rcv;
    clock_t clocker;
    vector<packet>packets;
    mutex base_mutex;
    mutex packets_mutex;
    mutex cwnd_mutex;
    int timeout;
    char *filepath;
    SOCKET sock;
    struct sockaddr_in cli_addr;
    int cli_addr_len;
public:
    sender(int base, int next_seqnum, int rwnd, int cwnd, int ssthresh, char fin, bool stop_timer, bool stop_rcv, int timeout, char *filepath, SOCKET sock, struct sockaddr_in cli_addr, int cli_addr_len) {
        this->base = base;
        this->next_seqnum = next_seqnum;
        this->rwnd = rwnd;
        this->cwnd = cwnd;
        this->ssthresh = ssthresh;
        this->fin = fin;
        this->stop_timer = stop_timer;
        this->stop_rcv = stop_rcv;
        this->timeout = timeout;
        this->filepath = filepath;
        this->sock = sock;
        this->cli_addr = cli_addr;
        this->cli_addr_len = cli_addr_len;
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

    void start() {
        ifstream file(filepath, ios::in|ios::binary);
        packet sndpkt;
        /* 此线程用于接收客户端的确认包，并更新base,packets,rwnd,cwnd以及ssthresh等 */
        thread rdt_rcv_thread(&sender::lget_rdt_rcv, this, ref(clocker), ref(base), ref(next_seqnum), ref(packets), sock, cli_addr, cli_addr_len, ref(rwnd), ref(cwnd), ref(ssthresh), ref(stop_rcv));

        /* 此线程用于定时器，超时重发 */
        thread time_out_thread(&sender::lget_time_out, this, ref(clocker), ref(base), ref(next_seqnum), ref(packets), sock, cli_addr, cli_addr_len, ref(rwnd), ref(cwnd), ref(ssthresh), ref(stop_timer));
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
};