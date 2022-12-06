#pragma once

#include "noncopyable.h"

#include <functional>
#include <thread>
#include <memory>
#include <unistd.h>
#include <string>
#include <atomic>

class Thread : noncopyable//线程类 
{
public:
    using ThreadFunc = std::function<void()>;

    explicit Thread(ThreadFunc, const std::string &name = std::string());
    ~Thread();

    void start();
    void join();

    bool started() const { return started_; }
    pid_t tid() const { return tid_; }//返回线程的tid 
    const std::string& name() const { return name_; }

    static int numCreated() { return numCreated_; }

private:
    void setDefaultName();//设置默认的名字

    bool started_;
    bool joined_;//当前线程等待其他线程运行完 再运行
    std::shared_ptr<std::thread> thread_;
    pid_t tid_;
    ThreadFunc func_;//存储线程函数
    std::string name_;//每个线程都有一个名字
    static std::atomic_int numCreated_;//记录产生线程的个数
};