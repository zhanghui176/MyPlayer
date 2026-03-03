#ifndef SYNCTIMER_H
#define SYNCTIMER_H
#include <math.h>
#include <QMutex>
#include <qdebug.h>
#include <qtimer.h>

extern "C" {
#include <libavutil/time.h>
}

class SyncTimer
{
public:
    SyncTimer(double v = 0.0)
        : frameRate(v)
    {
    }

    bool wait(bool shouldSync, double pts, double speed = 1.0, double master = -1)
    {
        QMutexLocker locker(&m_mutex);
        double delay = pts - prevPts;
        if (isnan(delay) || delay <= 0 || delay > maxFrameDuration)
            delay = frameRate;

        if (master > 0) {
            double diff = pts - master;
            double sync_threshold = qMax(minThreshold, qMin(maxThreshold, delay));
            if (!isnan(diff) && fabs(diff) < maxFrameDuration) {
                if (diff <= -sync_threshold)
                    delay = qMax(0.0, delay + diff);
                else if (diff >= sync_threshold && delay > frameDuplicationThreshold)
                    delay = delay + diff;
                else if (diff >= sync_threshold)
                    delay = 2 * delay;
            }
        }

        delay /= speed;
        const double time = av_gettime_relative() / 1000000.0;
        if (shouldSync) {
            if (pts < prevPts)
                return true;
            if (time < frameTimer + delay) {
                double remaining_time = qMin(frameTimer + delay - time, refreshRate);
                locker.unlock();
                // qDebug() << "wait for " << remaining_time * 1000000.0 << " us";
                av_usleep((int64_t)(remaining_time * 1000000.0));
                return false;
            }
        }

        prevPts = pts;
        frameTimer += delay;
        if ((delay > 0 && time - frameTimer > maxThreshold) || !shouldSync)
            frameTimer = time;

        return true;
    }

    double pts() const
    {
        QMutexLocker locker(&m_mutex);
        return prevPts;
    }

    void clear()
    {
        QMutexLocker locker(&m_mutex);
        prevPts = 0;
        frameTimer = 0;
    }

    void setFrameRate(double v)
    {
        QMutexLocker locker(&m_mutex);
        frameRate = v;
    }

private:
    double frameRate = 0;
    double frameTimer = 0;
    double prevPts = 0;
    mutable QMutex m_mutex;
    const double maxFrameDuration = 10.0;
    const double minThreshold = 0.04;
    const double maxThreshold = 0.1;
    const double frameDuplicationThreshold = 0.1;
    const double refreshRate = 0.01;
};

#endif // SYNCTIMER_H
