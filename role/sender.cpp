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
    clock_t timeout;
    char *filepath;
    SOCKET sock;
    struct sockaddr_in cli_addr;
    int cli_addr_len;
    enum CongestionStatus { SLOWSTART = 0, CONGESTIONAVOIDANCE, FASTRECOVERY } congestionStatus;
    enum Action { NEWACK = 0, DUPACK, TIMEOUT };
    int duplicate_ack;
    int ack_count;
public:
    sender(int base, int next_seqnum, int rwnd, int cwnd, int ssthresh, char fin, bool stop_timer, bool stop_rcv, clock_t timeout, char *filepath, SOCKET sock, struct sockaddr_in cli_addr, int cli_addr_len) {
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
        this->congestionStatus = SLOWSTART;
        this->duplicate_ack = 0;
        this->ack_count = 0;
        this->clocker = timeout;
    }

    void congestionControl(Action act) {
        switch (congestionStatus) {
            //慢启动
            case SLOWSTART: 
            if (act == NEWACK) {
                cwnd = cwnd + 1;
                duplicate_ack = 0;
            } else if (act == TIMEOUT) {
                ssthresh = cwnd / 2;
                cwnd = 1;
                duplicate_ack = 0;
            } else if (act == DUPACK) {
                duplicate_ack = duplicate_ack + 1;
            }
            if (duplicate_ack > 2) {
                ssthresh = cwnd / 2;
                cwnd = ssthresh + 3;
                congestionStatus = FASTRECOVERY;
            } else if (cwnd >= ssthresh) {
                congestionStatus = CONGESTIONAVOIDANCE;
            }
            break;
            //拥塞避免
            case CONGESTIONAVOIDANCE:  
            if (act == NEWACK) {
                ack_count += 1;
                if(ack_count > cwnd) {
                    cwnd += 1;
                    ack_count = 0;
                }
                duplicate_ack = 0;
            } else if (act == TIMEOUT) {
                ssthresh = cwnd / 2;
                cwnd = 1;
                congestionStatus = SLOWSTART;
                duplicate_ack = 0;
            } else if (act == DUPACK) {
                duplicate_ack = duplicate_ack + 1;
            }
            if (duplicate_ack > 2) {
                ssthresh = cwnd / 2;
                cwnd = ssthresh + 3;
                congestionStatus = FASTRECOVERY;
            }
            break;
            //快速恢复
            case FASTRECOVERY:  
            if (act == NEWACK) {
                cwnd = ssthresh;
                duplicate_ack = 0;
                congestionStatus = CONGESTIONAVOIDANCE;
            } else if (act == TIMEOUT) {
                ssthresh = cwnd / 2;
                cwnd = 1;
                congestionStatus = SLOWSTART;
                duplicate_ack = 0;
            } else if (act == DUPACK) {
                cwnd = cwnd + 1;
            }
        }
    }

    void lget_rdt_rcv() {
        packet sndpkt;
        // printf("lget_rdt_rcv\n");
        while(true) {
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
            // cout<<"base: "<<base<<endl;
            printf("receive ack packet: SEQ: %d, ACK: %d, FIN: %c.\n", rcvpkt.seq, rcvpkt.ack, rcvpkt.fin);
            if(base <= rcvpkt.ack) {
                packets_mutex.lock();
                auto iter = packets.begin();
                while(iter->seq <= rcvpkt.ack) {
                    congestionControl(NEWACK);
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
                    cout << "receive the final ack packet." <<endl;
                    sndpkt = packet(next_seqnum, rcvpkt.seq, 0, '1', "", rcvpkt.fin, 0, "");
                    cout << "send the final ack packet." <<endl;
                    int sendnum = sendto(sock, (char*)&sndpkt, sizeof(sndpkt), 0, (struct sockaddr*)&cli_addr, cli_addr_len);
                    if(sendnum < 0) {
                        cerr<<"sendto error"<<endl;
                    }
                    packets.push_back(sndpkt);
                    next_seqnum += 1;
                }
            } else {
                congestionControl(DUPACK);
            }
        }
    }

    void lget_time_out() {
        // printf("lget_time_out\n");
        while(true) {
            if(stop_timer){
                printf("exit the time out thread.\n");
                break;
            }
            clock_t now = clock();
            if(now - clocker > timeout && packets.size() > 0) {
                congestionControl(TIMEOUT);
                base_mutex.lock();
                base = packets.front().seq;
                base_mutex.unlock();
                clocker = clock();
                packets_mutex.lock();
                for(int i=0; i<int(packets.size()); i++) {
                    printf("send pkt again: SEQ: %d, ACK: %d, FIN: %c.\n", packets[i].seq, packets[i].ack, packets[i].fin);
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
        // thread rdt_rcv_thread(&sender::lget_rdt_rcv, this, ref(clocker), ref(base), ref(next_seqnum), ref(packets), sock, cli_addr, cli_addr_len, ref(rwnd), ref(cwnd), ref(ssthresh), ref(stop_rcv));
        thread rdt_rcv_thread(&sender::lget_rdt_rcv, this);

        /* 此线程用于定时器，超时重发 */
        thread time_out_thread(&sender::lget_time_out, this);
        // printf("%d %d %d\n", base, next_seqnum, rwnd);
        /* 主线程用于发送文件 */
        while(true) {
            // printf("%d %d %d %d %d %d***\n", base, next_seqnum, rwnd, cwnd, congestionStatus, ssthresh);
            if (next_seqnum < base + min(rwnd, cwnd)) {
                char buf[DATA_LEN];
                file.read(buf, DATA_LEN);
                if (file.eof()) {
                    cout<<"file end."<<endl;
                    fin = '1';
                }
                cerr << buf <<endl;
                sndpkt = packet(next_seqnum, 0, 0, '1', "", fin, file.gcount(), buf);
                printf("send data packet: SEQ: %d, ACK: %d, FIN: %c.\n", sndpkt.seq, sndpkt.ack, sndpkt.fin);
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
    }
};