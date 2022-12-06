#pragma once

#include "noncopyable.h"
#include "Thread.h"

#include <functional>
#include <mutex>
#include <condition_variable>
#include <string>

class EventLoop;

class EventLoopThread : noncopyable
{
public:
    using ThreadInitCallback = std::function<void(EventLoop*)>; 

    EventLoopThread(const ThreadInitCallback &cb = ThreadInitCallback(), 
        const std::string &name = std::string());
    ~EventLoopThread();

    EventLoop* startLoop();

private:
    void threadFunc();

    EventLoop *loop_;
    bool exiting_;//是否退出循环 
    Thread thread_;
    std::mutex mutex_;//互斥锁
    std::condition_variable cond_;//条件变量 
    ThreadInitCallback callback_;//初始化操作
};