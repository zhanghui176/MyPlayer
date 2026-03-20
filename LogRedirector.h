// 日志重定向工具类
// 用法：调用 LogRedirector::init() 即可将所有 qDebug/qWarning/qCritical 输出重定向到 log.txt

#include <QFile>
#include <QTextStream>
#include <QDebug>
#include <QDir>

// 日志重定向工具类
#include <QFile>
#include <QTextStream>
#include <QDebug>
#include <QDir>
#include <QDateTime>
#include <QThread>
#include <QMutex>

namespace {
    static QFile logFile;
    static QTextStream logStream;
    static QMutex logMutex;
}

class LogRedirector {
public:
    static void init() {
        QString logDir = QDir::current().filePath("log");
        QDir().mkpath(logDir);
        logFile.setFileName(QDir(logDir).filePath("log.txt"));
        logFile.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text);
        logStream.setDevice(&logFile);
        qInstallMessageHandler(messageHandler);
    }

private:
    static void messageHandler(QtMsgType type, const QMessageLogContext &context, const QString &msg) {
        QMutexLocker locker(&logMutex);
        QString typeStr;
        switch (type) {
        case QtInfoMsg:     typeStr = "[INFO]"; break;
        case QtDebugMsg:    typeStr = "[DEBUG]"; break;
        case QtWarningMsg:  typeStr = "[WARN]"; break;
        case QtCriticalMsg: typeStr = "[CRIT]"; break;
        case QtFatalMsg:    typeStr = "[FATAL]"; break;
        }
        QString timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss.zzz");
        quint64 threadId = quint64(QThread::currentThreadId());
        logStream << "[" << timestamp << "]"
                  << "[TID:" << threadId << "]"
                  << typeStr << " " << msg << "\n";
        logStream.flush();
        if (type == QtFatalMsg) abort();
    }
};
