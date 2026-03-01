#ifndef QAVPACKET_H
#define QAVPACKET_H

extern "C" {
#include <libavformat/avformat.h>
}

class QAVPacket
{
public:
    QAVPacket();
    ~QAVPacket();
    AVPacket* packet();
    void SetPts();
    double GetPts();
    void SetDuration();
    double GetDuration();
private:
    AVPacket *pkt_ = nullptr;
    double pts_;
    double duration_;
};

#endif // QAVPACKET_H
