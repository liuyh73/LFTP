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
using namespace std;
class receiver {
private:
    int bufSize = 128;
    int rwnd = 128;
    int expected_seqnum = 1;
    clock_t clocker;
    bool stop_timer = false;
    queue<packet>pkts_buf;
    SOCKET sock;
    struct sockaddr_in svc_addr;
    int svc_addr_len;
    int timeout;
    char *filepath;
    mutex pkts_buf_mutex;
    mutex rwnd_mutex;
public:
    receiver(int bufSize, int rwnd, int expected_seqnum, bool stop_timer, int timeout, char *filepath, SOCKET sock, struct sockaddr_in svc_addr, int svc_addr_len) {
        this->bufSize = bufSize;
        this->rwnd = rwnd;
        this->expected_seqnum = expected_seqnum;
        this->stop_timer = stop_timer;
        this->timeout = timeout;
        this->filepath = filepath;
        this->sock = sock;
        this->svc_addr = svc_addr;
        this->svc_addr_len = svc_addr_len;
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

    void start() {
        packet rcvpkt, sndpkt;
        ofstream file(filepath, ios::out|ios::binary);
        thread read_from_buf_thread(&receiver::readFromBuf, this, ref(file), ref(pkts_buf), ref(bufSize), ref(rwnd));
    
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
};