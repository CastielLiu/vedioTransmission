#ifndef UTILS_H
#define UTILS_H

#include "ui_mainwindow.h"

#include<iostream>
#include<sstream>
#include<stdint.h>
#include<cstring>
#include <QObject>
#include <QtNetwork/QUdpSocket>
#include <QHostAddress>
#include <QThread>
#include <thread>

// 消息类型
enum dataType
{
    Video=0,          //视频消息
    Audio=1,          //语音
    RequestConnect=2, //请求连接
    AcceptConnect=3,  //接收连接
    RefuseConnect=4,  //拒绝连接
    DisConnect=5,     //挂断连接
    NoneType = 6,
    RegisterOK=7,     //注册成功(服务器发往客户端)
    RegisterFail=8,   //注册失败(服务器发往客户端)
    RequestRegister=9,//请求注册到服务器
    ResponseRegister=10,//回应客户端请求(包含服务端服务端口号)
    HeartBeat = 11, //心跳包
};

//数据传输头
#pragma pack(push,1)
typedef struct TransPack
{
    uint8_t head[2];
    uint16_t length;
    uint8_t type;
    uint8_t checkNum;

    uint16_t senderId;
    uint16_t receiverId;

    TransPack(dataType t = NoneType)
    {
        head[0] = 0x66;
        head[1] = 0xcc;
        type = t;
        length = checkNum = 0;
    }
} transPack_t;

#pragma pack(pop)

enum systemStatus
{
    SystemIdle,     //空闲
    SystemBusy,     //忙(正在呼叫、正在被叫)
    SystemRunning,  //正在通话
    SystemRefused,  //请求被拒绝
    SystemAccepted, //请求被接受
};

extern const QHostAddress g_serverIp;
extern const quint16 g_registerPort;
extern int g_registerStatus;
extern quint16 g_msgPort;
extern systemStatus g_systemStatus;
extern uint16_t g_myId;
extern uint16_t g_otherId ;
extern bool g_isCaller;

//extern bool
extern Ui::MainWindow *g_ui;

int ipConvert(const std::string& ip_str);
std::string ipConvert(const int ip_int);


#endif // UTILS_H
