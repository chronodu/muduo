#include "Thread.h"
#include "CurrentThread.h"

#include <semaphore.h>//信号量

std::atomic_int Thread::numCreated_(0);//记录产生线程的个数

Thread::Thread(ThreadFunc func, const std::string &name)
    : started_(false)
    , joined_(false)
    , tid_(0)
    , func_(std::move(func))
    , name_(name)
{
    setDefaultName();//设置默认的名字
}

Thread::~Thread()
{
    if (started_ && !joined_)//线程运行起来才需要做析构
    {
        thread_->detach(); // thread类提供的设置分离线程的方法（守护线程） 不用担心出现孤儿线程
    }
}

//线程的启动
void Thread::start()  // 一个Thread对象，记录的就是一个新线程的详细信息
{
    started_ = true;
    sem_t sem;//信号量
    sem_init(&sem, false, 0);

    // 开启线程
    thread_ = std::shared_ptr<std::thread>(new std::thread([&](){
        // 获取线程的tid值
        tid_ = CurrentThread::tid();
        sem_post(&sem);

        // 开启一个新线程，专门执行该线程函数
        func_(); 
    }));

    // 这里必须等待获取上面新创建的线程的tid值
    sem_wait(&sem);
}

void Thread::join()
{
    joined_ = true;
    thread_->join();
}

void Thread::setDefaultName()
{
    int num = ++numCreated_;
    if (name_.empty())//线程还没有名字 就给他一个名字
    {
        char buf[32] = {0};
        snprintf(buf, sizeof buf, "Thread%d", num);//序号作为名字
        name_ = buf;
    }
}