#include <iostream>
#include <cstring>
#define DATA_LEN 2024
#define PACKET_LEN 2048
using namespace std;
struct packet{
    int seq;
    int ack;
    int rwnd;
    int len;
    char cmd[6];
    char status;
    char fin;
    char data[DATA_LEN];
    packet(){}
    packet(int seq, int ack, int rwnd, char status, const char *cmd, char fin, int len, const char *data){
        this->seq = seq;
        this->ack = ack;
        this->rwnd = rwnd;
        this->status = status;
        memset(this->cmd, 0, sizeof(this->cmd));
        strcpy(this->cmd, cmd);
        this->fin = fin;
        this->len = len;
        memcpy(this->data, data, len);
        this->data[len] = '\0';
    }
};