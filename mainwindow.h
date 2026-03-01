#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include "AVPlayer.h"
#include <QTimer>
#include "OpenGLVideoWidget.h"

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void on_pushButton_clicked();

    void on_pushButton_2_clicked();

    void on_horizontalSlider_sliderReleased();

    void onProgressUpdate();

    void on_progressSlider_sliderPressed();

    void on_progressSlider_sliderReleased();

private:
    Ui::MainWindow *ui;
    AVPlayer player_;
    QTimer progressTimer_;
    double duration_;
    bool sliderDragging_ = false;
};
#endif // MAINWINDOW_H
