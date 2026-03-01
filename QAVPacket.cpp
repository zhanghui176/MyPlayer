#include "QAVPacket.h"
#include <QDebug>

QAVPacket::QAVPacket()
{
    qDebug()<< "QAVpakcet construct";
    pkt_ = av_packet_alloc();
    pkt_->size = 0;
    pkt_->stream_index = -1;
    pkt_->pts = AV_NOPTS_VALUE;
}

QAVPacket::~QAVPacket()
{
    av_packet_free(&pkt_);
}

AVPacket* QAVPacket::packet()
{
    return pkt_;
}

