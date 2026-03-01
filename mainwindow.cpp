#include "mainwindow.h"
#include "ui_mainwindow.h"
#include <QFileDialog>
#include <QSignalBlocker>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);
    OpenGLVideoWidget *glwidget = qobject_cast<OpenGLVideoWidget*>(ui->openGLWidget);
    auto conn1 = connect(&player_, &AVPlayer::videoframeReady,
                        glwidget, &OpenGLVideoWidget::setYUV420PFrame, Qt::QueuedConnection);

    ui->progressSlider->setRange(0,1000);
    ui->progressSlider->setValue(0);

    progressTimer_.setInterval(100);
    connect(&progressTimer_, &QTimer::timeout, this, &MainWindow::onProgressUpdate);
    progressTimer_.start();
}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::on_pushButton_clicked()
{
    player_.stop();
}


void MainWindow::on_pushButton_2_clicked()
{
    QString filename = QFileDialog::getOpenFileName(this, "Open Video", "", "Video Files (*.mp4 *.avi *.mov)");
    if(filename.isEmpty()) return;
    player_.setUrl(filename.toStdString());
    player_.doload();
    duration_ = player_.getDurationSec();
}

void MainWindow::onProgressUpdate()
{
    if (!ui->progressSlider || sliderDragging_ || duration_ <= 0.0)
    {
        return;
    }

    const double currentTime = player_.getCurrentTimeSec();
    const double ratio = qBound(0.0, currentTime/duration_, 1.0);
    const int value = static_cast<int> (ratio * ui->progressSlider->maximum());

    QSignalBlocker blocker(ui->progressSlider);
    ui->progressSlider->setValue(value);
}

void MainWindow::on_progressSlider_sliderPressed()
{
    sliderDragging_ = true;
}

void MainWindow::on_progressSlider_sliderReleased()
{
    if (!ui->progressSlider)
    {
        return;
    }

    const int maxValue = ui->progressSlider->maximum();
    if (maxValue <= 0)
    {
        sliderDragging_ = false;
        return;
    }

    const double pos = static_cast<double>(ui->progressSlider->value()) / static_cast<double>(maxValue);
    player_.doSeek(pos);
    qDebug() << "doSeek to pos : " << pos;
    sliderDragging_ = false;
}

