#pragma once

#include "noncopyable.h"

class InetAddress;

// 封装socket fd
class Socket : noncopyable
{
public:
    explicit Socket(int sockfd)
        : sockfd_(sockfd)
    {}

    ~Socket();

    int fd() const { return sockfd_; }
    void bindAddress(const InetAddress &localaddr);
    void listen();
    int accept(InetAddress *peeraddr);

    void shutdownWrite();

    //更改TCP选项的
    void setTcpNoDelay(bool on);//直接发送 对于TCP数据不进行缓冲
    void setReuseAddr(bool on);//允许重复使用本地地址
    void setReusePort(bool on);//允许重复使用本地端口
    void setKeepAlive(bool on);//确定连接可用性
private:
    const int sockfd_;
};