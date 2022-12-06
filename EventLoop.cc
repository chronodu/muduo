#include "EventLoop.h"
#include "Logger.h"
#include "Poller.h"
#include "Channel.h"

#include <sys/eventfd.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <memory>

// 防止一个线程创建多个EventLoop   thread_local 通过这个全局变量指针来控制的 
__thread EventLoop *t_loopInThisThread = nullptr;

// 定义默认的Poller IO复用接口的超时时间
const int kPollTimeMs = 10000;

// 创建wakeupfd，用来notify唤醒subReactor处理新来的channel
int createEventfd()
{
    int evtfd = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (evtfd < 0)//出错 
    {
        LOG_FATAL("eventfd error:%d \n", errno);
    }
    return evtfd;
}

EventLoop::EventLoop()
    : looping_(false)//刚开始还没有开启循环
    , quit_(false)
    , callingPendingFunctors_(false)//刚开始没有需要处理的回调
    , threadId_(CurrentThread::tid())//获取线程id
    , poller_(Poller::newDefaultPoller(this))
    , wakeupFd_(createEventfd())
    , wakeupChannel_(new Channel(this, wakeupFd_))
{
    LOG_DEBUG("EventLoop created %p in thread %d \n", this, threadId_);
    if (t_loopInThisThread)//不为空 有了一个loop了
    {
        LOG_FATAL("Another EventLoop %p exists in this thread %d \n", t_loopInThisThread, threadId_);
    }
    else
    {
        t_loopInThisThread = this;
    }

    // 设置wakeupfd的事件类型以及发生事件后的回调操作 就是唤醒EventLoop 
    wakeupChannel_->setReadCallback(std::bind(&EventLoop::handleRead, this));

    // 每一个eventloop都将监听wakeupchannel的EPOLLIN读事件了
    wakeupChannel_->enableReading();
    //main reactor可以通过给weakupfd 写东西来通知subreactor
}

EventLoop::~EventLoop()
{
    wakeupChannel_->disableAll();//disableAll把所有的事件都置为了kNoneEvent 对所有事件都不感兴趣了
    wakeupChannel_->remove();//把channel本身从POller中移除
    ::close(wakeupFd_);
    t_loopInThisThread = nullptr;
}

// 开启事件循环
void EventLoop::loop() //调度底层的POller调度事件分发器（多路选择器） 
{
    looping_ = true;//开启循环
    quit_ = false;//没有退出

    LOG_INFO("EventLoop %p start looping \n", this);

    while(!quit_)
    {
        activeChannels_.clear();
        // 监听两类fd   一种是client的fd（跟客户端通信的fd channel），
        //一种wakeupfd（mainloop 跟 subloop通信）mainloop唤醒subloop
        pollReturnTime_ = poller_->poll(kPollTimeMs, &activeChannels_);//EventLoop阻塞在这个位置
        for (Channel *channel : activeChannels_)//得到了发生事件的channel 就遍历他
        {
            // Poller监听哪些channel发生事件了，然后上报给EventLoop，通知channel处理相应的事件
            channel->handleEvent(pollReturnTime_);
        }
        // 执行当前EventLoop事件循环需要处理的回调操作
        /**
         * IO线程 mainLoop accept fd《=channel subloop
         * mainLoop 事先注册一个回调cb（需要subloop来执行）   
         * wakeup subloop后，执行下面的方法，执行之前mainloop注册的cb操作
         */ 
        doPendingFunctors();
    }

    LOG_INFO("EventLoop %p stop looping. \n", this);
    looping_ = false;
}


// 退出事件循环  1.loop在自己的线程中调用quit  2.在非loop的线程中，调用loop的quit
/**
 *              mainLoop
 * 
 *  muduo库里面没有这个生产者-消费者 他们之间是直接通信的 就是通过wakeupfd来进行线程间的唤醒
 *  no ==================== 生产者-消费者的线程安全的队列
 * 
 *  subLoop1     subLoop2     subLoop3
 * 每一个loop里面都有一个wakeupfd可以唤醒
 */ 
void EventLoop::quit()
{
    quit_ = true;

    // 如果是在其它线程中，调用的quit   在一个subloop(woker)中，调用了mainLoop(IO主线程)的quit
    if (!isInLoopThread())  
    {
        wakeup();//现在不知道主线程是什么情况 需要把他唤醒
    }
}

// 在当前loop中执行cb callback 
void EventLoop::runInLoop(Functor cb)
{
    if (isInLoopThread()) // 在当前的loop线程中，执行cb
    {
        cb();
    }
    else // 在非当前loop线程中执行cb , 就需要唤醒loop所在线程，执行cb
    {
        queueInLoop(cb);//放入缓存队列
    }
}
// 把cb放入队列中，唤醒loop所在的线程，执行cb
void EventLoop::queueInLoop(Functor cb)//缓存起来 不在当前的loop执行
{
    {
        std::unique_lock<std::mutex> lock(mutex_);
        pendingFunctors_.emplace_back(cb);//push_back是拷贝构造 emplace_back 是直接构造
    }

    // 唤醒相应的，需要执行上面回调操作的loop的线程了
    // || callingPendingFunctors_的意思是：当前loop正在执行回调，但是loop又有了新的回调
    if (!isInLoopThread() || callingPendingFunctors_) 
    {
        wakeup(); // 唤醒loop所在线程
    }
}

void EventLoop::handleRead()
{
  uint64_t one = 1;
  ssize_t n = read(wakeupFd_, &one, sizeof one);//1.fd 2.buffer 3.size 
  if (n != sizeof one)
  {
    LOG_ERROR("EventLoop::handleRead() reads %lu bytes instead of 8", n);
  }
}

// 用来唤醒loop所在的线程的  向wakeupfd_写一个数据，wakeupChannel就发生读事件，当前loop线程就会被唤醒
void EventLoop::wakeup()
{
    uint64_t one = 1;
    ssize_t n = write(wakeupFd_, &one, sizeof one);
    if (n != sizeof one)//写数据的返回值跟缓存区大小不一样 
    {
        LOG_ERROR("EventLoop::wakeup() writes %lu bytes instead of 8 \n", n);
    }
}

// EventLoop的方法 =》 Poller的方法
void EventLoop::updateChannel(Channel *channel)
{
    poller_->updateChannel(channel);
}

void EventLoop::removeChannel(Channel *channel)
{
    poller_->removeChannel(channel);
}

bool EventLoop::hasChannel(Channel *channel)
{
    return poller_->hasChannel(channel);
}

void EventLoop::doPendingFunctors() // 执行回调
{
    std::vector<Functor> functors;//局部的装回调的vector
    callingPendingFunctors_ = true;//标识当前loop是否有需要执行的回调操作

    {
        std::unique_lock<std::mutex> lock(mutex_);
        functors.swap(pendingFunctors_);
        /*
        
        pendingFunctors_跟局部的functors进行了交换 相当于把pendingFunctors_置空
        把需要待执行的回调 全部放到这个局部的functors里面 为什么要这样做？
        
        把下面那个pendingFunctors_解放了  这儿和下面的queueInLoop两个地方可以并发操作
        
        */

        /*
        queueInLoop里面的操作 
         std::unique_lock<std::mutex> lock(mutex_);
        pendingFunctors_.emplace_back(cb);//push_back是拷贝构造 emplace_back 是直接构造
        */
    }

    for (const Functor &functor : functors)
    {
        functor(); // 执行当前loop需要执行的回调操作
    }

    callingPendingFunctors_ = false;
}