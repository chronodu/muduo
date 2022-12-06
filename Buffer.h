#pragma once

#include <vector>
#include <string>
#include <algorithm>

// 网络库底层的缓冲器类型定义
class Buffer
{
public:
    static const size_t kCheapPrepend = 8;//记录数据包的长度 prependable bytes 8个字节的长度
    static const size_t kInitialSize = 1024;//readable + writable 缓冲区的大小 1024 1k

    explicit Buffer(size_t initialSize = kInitialSize)
        : buffer_(kCheapPrepend + initialSize)//buffer大小 8+1024
        , readerIndex_(kCheapPrepend)//readerIndex_和writerIndex_刚开始都指向一个地方
        , writerIndex_(kCheapPrepend)
    {}

    size_t readableBytes() const //可读数据的长度 
    {
        return writerIndex_ - readerIndex_;
    }

    size_t writableBytes() const
    {
        return buffer_.size() - writerIndex_;//1024+8=整个buffer的长度  
        //0~size - 0~writerIndex_ = writerIndex_
    }

    size_t prependableBytes() const
    {
        return readerIndex_;//0~readerIndex_ 这个部分就是prependableBytes
    }

    // 返回缓冲区中可读数据的起始地址
    const char* peek() const
    {
        return begin() + readerIndex_;
        //     起始地址
    }

    // onMessage string <-转为 Buffer
    void retrieve(size_t len)
    {
        if (len < readableBytes())
        {
            readerIndex_ += len; // 应用只读取了刻度缓冲区数据的一部分，就是len，还剩下readerIndex_ += len -> writerIndex_
        }
        else   // len == readableBytes()
        {
            retrieveAll();
        }
    }

    void retrieveAll()
    {
        readerIndex_ = writerIndex_ = kCheapPrepend;
    }

    // 把onMessage函数上报的Buffer数据，转成string类型的数据返回
    std::string retrieveAllAsString()
    {
        return retrieveAsString(readableBytes()); // 应用可读取数据的长度
    }

    std::string retrieveAsString(size_t len)
    {
        std::string result(peek(), len);
        retrieve(len); // 上面一句把缓冲区中可读的数据，已经读取出来，这里肯定要对缓冲区进行复位操作
        return result;
    }

    // buffer_.size() - writerIndex_    len
    void ensureWriteableBytes(size_t len)//往缓冲区写 如果len <= buffer_.size() - writerIndex就直接写
    {
        if (writableBytes() < len)//
        {
            makeSpace(len); // 扩容函数
        }
    }

    // 把[data, data+len]内存上的数据，添加到writable缓冲区当中
    void append(const char *data, size_t len)
    {
        ensureWriteableBytes(len);//保证有这么长的空间可以写 
        std::copy(data, data+len, beginWrite());//
        writerIndex_ += len;
    }

    char* beginWrite()
    {
        return begin() + writerIndex_;
    }

    const char* beginWrite() const
    {
        return begin() + writerIndex_;
    }

    // 从fd上读取数据
    ssize_t readFd(int fd, int* saveErrno);

    // 通过fd发送数据
    ssize_t writeFd(int fd, int* saveErrno);
private:
    char* begin()
    {
        // it.operator*().operator&() 
        return &*buffer_.begin();  // vector底层数组首元素的地址，也就是数组的起始地址
    }
    const char* begin() const//供常对象使用
    {
        return &*buffer_.begin();
    }
    void makeSpace(size_t len)//保证底层的空间充足
    {
        /*
        len是要写的长度
        kCheapPrepend  | reader | writer |
        kCheapPrepend  |  len            |

        中间的reader已经不能再写东西了 他是缓冲起来给应用去读的
        0~readerIndex_ 这个部分就是prependableBytes
        kCheapPrepend 8 

        */
        if (writableBytes() + prependableBytes() < len + kCheapPrepend)
        {
            buffer_.resize(writerIndex_ + len);
        }
        else//有空闲的空间的话 就把中间未读取的数据向前靠 把前面空闲的空间让到后面来了 
        {
            size_t readalbe = readableBytes();//readalbe是未读的那一块儿的长度
            std::copy(begin() + readerIndex_, 
                    begin() + writerIndex_,//未读部分 
                    begin() + kCheapPrepend);//    1 已读  1 空闲  /
                                            //     1 空闲  /
            readerIndex_ = kCheapPrepend;//跑到第8个来了
            writerIndex_ = readerIndex_ + readalbe;
        }
    }

    std::vector<char> buffer_;
    size_t readerIndex_;//数据可读下标
    size_t writerIndex_;//数据可写下标
};