#include "mainwindow.h"

#include <QApplication>
#include <QDebug>

#include "LogRedirector.h"

extern "C" {
#include <libavformat/avformat.h>
#include <libavutil/opt.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);

    LogRedirector::init(); // 日志重定向到 build/log/log.txt

    // 全局的网络初始化，必须在使用FFmpeg的网络功能前调用一次
    const int networkInitRet = avformat_network_init(); 
    if (networkInitRet < 0)
    {
        qWarning() << "avformat_network_init failed:" << networkInitRet;
    }

    MainWindow w;
    w.show();
    const int exitCode = a.exec();
    avformat_network_deinit();
    return exitCode;
}
