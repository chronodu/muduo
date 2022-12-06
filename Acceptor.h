#pragma once
#include "noncopyable.h"
#include "Socket.h"
#include "Channel.h"

#include <functional>

class EventLoop;
class InetAddress;

class Acceptor : noncopyable//Acceptor运行在mainloop里面
{
public:
    using NewConnectionCallback = std::function<void(int sockfd, const InetAddress&)>;
    Acceptor(EventLoop *loop, const InetAddress &listenAddr, bool reuseport);
    ~Acceptor();

    void setNewConnectionCallback(const NewConnectionCallback &cb) 
    {
        newConnectionCallback_ = cb;
    }

    bool listenning() const { return listenning_; }
    void listen();
private:
    void handleRead();
    
    EventLoop *loop_; // Acceptor用的就是用户定义的那个baseLoop，也称作mainLoop
    Socket acceptSocket_;//acceptor本质上底层就是一个listenfd
    Channel acceptChannel_;//打包的就是一个fd（把fd打包成channel） & 跟fd感兴趣的事件 & 以及一个revents 
    //client conn success客户端连接成功 TCPServer应该选择这个subreactor唤醒 

    NewConnectionCallback newConnectionCallback_;//来了一条新连接的回调
    //打包成channel再通过getnextloop 唤醒一个subloop 然后再把这个channel分发给相应的loop
    //去监听已连接用户的读写事件 

    bool listenning_;
};