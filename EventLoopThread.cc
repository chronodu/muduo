#include "EventLoopThread.h"
#include "EventLoop.h"


EventLoopThread::EventLoopThread(const ThreadInitCallback &cb, 
        const std::string &name)
        : loop_(nullptr)//才创建一个loop对象 所以为空
        , exiting_(false)
        , thread_(std::bind(&EventLoopThread::threadFunc, this), name)
        , mutex_()
        , cond_()
        , callback_(cb)
{

}

EventLoopThread::~EventLoopThread()
{
    exiting_ = true;
    if (loop_ != nullptr)
    {
        loop_->quit();
        thread_.join();
    }
}

EventLoop* EventLoopThread::startLoop()
{
    thread_.start(); // 启动底层的新线程

    EventLoop *loop = nullptr;

    {
        std::unique_lock<std::mutex> lock(mutex_);
        while ( loop_ == nullptr )//还没有执行到这儿
        {
            cond_.wait(lock);//等待这把锁
        }
        loop = loop_;
    }

    return loop;
}

// 下面这个方法，是在单独的新线程里面运行的
void EventLoopThread::threadFunc()
{
    EventLoop loop; // 创建一个独立的eventloop，和上面的线程是一一对应的
    //one loop per thread

    if (callback_)
    {
        callback_(&loop);
    }

    {
        std::unique_lock<std::mutex> lock(mutex_);
        loop_ = &loop;//loop指针指向loop对象
        cond_.notify_one();//通知一个
    }

    loop.loop(); // EventLoop loop  => Poller.poll 进入阻塞状态 开启远端的连接
    std::unique_lock<std::mutex> lock(mutex_);
    loop_ = nullptr;//底层的loop返回了
}