#ifndef PACKETQUEUE_H
#define PACKETQUEUE_H

#include <QList>
#include <QMutex>
#include "QAVPacket.h"
#include <condition_variable>

template<class T>
class PacketQueue
{
public:
    PacketQueue(AVMediaType mediaType, uint size);
    ~PacketQueue();
    void enqueue(QAVPacket pkt);
    std::optional<T> dequeue();

    void clear();
private:
    QList<T> decodePacketQueue_;
    QList<QAVPacket> originalPacketQueue_;

    mutable std::mutex mutex_;
    AVMediaType mediaType_;
    uint size_;
    std::mutex consumerMutex_;
    std::condition_variable consumerCv_;
    std::mutex producerMutex_;
    std::condition_variable producerCv_;
    bool abort_ = false;
};

#endif // PACKETQUEUE_H
