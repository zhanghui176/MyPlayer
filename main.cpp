#include "mainwindow.h"

#include <QApplication>

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

    MainWindow w;
    w.show();
    return a.exec();
}
