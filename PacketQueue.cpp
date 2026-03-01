#include "PacketQueue.h"
#include <QDebug>

template <typename T>
PacketQueue<T>::PacketQueue(AVMediaType mediaType, uint size)
    : mediaType_(mediaType)
    , size_(size)
    , abort_(false)
{
    qDebug() << "PacketQueue construct, queue size is " << size_;
}

template <typename T>
PacketQueue<T>::~PacketQueue()
{
    clear();
}

template <typename T>
void PacketQueue<T>::enqueue(QAVPacket pkt)
{
    std::unique_lock<std::mutex> lock(mutex_);

    producerCv_.wait(lock, [&]{return abort_ || originalPacketQueue_.size() < size_;});

    if (abort_)
    {
        return;
    }
    originalPacketQueue_.push_back(pkt);
    consumerCv_.notify_one();
}

template <typename T>
std::optional<T> PacketQueue<T>::dequeue()
{
    std::unique_lock<std::mutex> lock(mutex_);

    consumerCv_.wait(lock, [&]{return abort_ || !originalPacketQueue_.empty(); });

    if (abort_)
    {
        return std::nullopt;
    }
    auto pkt = originalPacketQueue_.front();
    // TODO decode
    originalPacketQueue_.pop_front();

    producerCv_.notify_one();
    return std::nullopt;
}

template <typename T>
void PacketQueue<T>::clear()
{
    std::unique_lock<std::mutex> lock(mutex_);
    abort_ = true;
    originalPacketQueue_.clear();
    decodePacketQueue_.clear();
    producerCv_.notify_all();
    consumerCv_.notify_all();
}
