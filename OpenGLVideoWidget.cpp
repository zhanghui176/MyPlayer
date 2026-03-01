#include "OpenGLVideoWidget.h"
#include <QDebug>
#include <cstring>
#include <iostream>
#include <QResource>

OpenGLVideoWidget::OpenGLVideoWidget(QWidget *parent)
    : QOpenGLWidget(parent),
    program(nullptr),
    vao(0),
    vbo(0),
    texY(0),
    texU(0),
    texV(0),
    yPlane(nullptr),
    uPlane(nullptr),
    vPlane(nullptr),
    frameWidth_(0),
    frameHeight_(0)
{}

OpenGLVideoWidget::~OpenGLVideoWidget() {
    makeCurrent();
    if(program) delete program;
    if(vbo) glDeleteBuffers(1, &vbo);
    if(vao) glDeleteVertexArrays(1, &vao);
    if(texY) glDeleteTextures(1, &texY);
    if(texU) glDeleteTextures(1, &texU);
    if(texV) glDeleteTextures(1, &texV);
    delete[] yPlane;
    delete[] uPlane;
    delete[] vPlane;
    doneCurrent();
}

void OpenGLVideoWidget::initializeGL() {
    initializeOpenGLFunctions();

    // 编译 shader
    program = new QOpenGLShaderProgram();
    if (QResource(":/vertex.glsl").isValid())
    {
        qDebug() << "vertex.glsl is packaged";
    }
    else
    {
        qDebug() << "vertex.glsl unpackaged";
    }
    program->addShaderFromSourceFile(QOpenGLShader::Vertex, ":/vertex.glsl");
    program->addShaderFromSourceFile(QOpenGLShader::Fragment, ":/fragment_yuv420p.glsl");
    program->link();

    // 顶点 + 纹理坐标
    float vertices[] = {
        // pos // tex
        -1.0f, -1.0f, 0.0f, 1.0f, // 左下
        1.0f, -1.0f, 1.0f, 1.0f, // 右下
        1.0f, 1.0f, 1.0f, 0.0f, // 右上
        -1.0f, 1.0f, 0.0f, 0.0f // 左上
    };

    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);

    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_DYNAMIC_DRAW);

    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);

    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
    glEnableVertexAttribArray(1);

    glBindVertexArray(0);

    // 创建纹理
    glGenTextures(1, &texY);
    glGenTextures(1, &texU);
    glGenTextures(1, &texV);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
}

void OpenGLVideoWidget::resizeGL(int w, int h) {
    glViewport(0, 0, w, h);
}

void OpenGLVideoWidget::paintGL() {
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);

    if(frameWidth_ == 0 || frameHeight_ == 0 || !yPlane) return;

    // 保持长宽比
    float widgetRatio = float(width()) / height();
    float videoRatio = float(frameWidth_) / frameHeight_;
    float scaleX = 1.0f, scaleY = 1.0f;
    if(widgetRatio > videoRatio)
        scaleX = videoRatio / widgetRatio;
    else
        scaleY = widgetRatio / videoRatio;

    // 更新顶点坐标（包含纹理坐标不变）
    float vertices[] = {
        -scaleX, -scaleY, 0.0f, 1.0f,
        scaleX, -scaleY, 1.0f, 1.0f,
        scaleX, scaleY, 1.0f, 0.0f,
        -scaleX, scaleY, 0.0f, 0.0f
    };
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(vertices), vertices);

    // 上传纹理
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texY);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, frameWidth_, frameHeight_, 0, GL_RED, GL_UNSIGNED_BYTE, yPlane);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, texU);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, frameWidth_/2, frameHeight_/2, 0, GL_RED, GL_UNSIGNED_BYTE, uPlane);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, texV);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, frameWidth_/2, frameHeight_/2, 0, GL_RED, GL_UNSIGNED_BYTE, vPlane);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    // 渲染
    program->bind();
    program->setUniformValue("texY", 0);
    program->setUniformValue("texU", 1);
    program->setUniformValue("texV", 2);

    glBindVertexArray(vao);
    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
    glBindVertexArray(0);
    program->release();
}

void OpenGLVideoWidget::setYUV420PFrame(AVFrame* frame) {

    qDebug() << "setYUV420PFrame height: " << frame->height  << "width is " << frame->width;
    int width = frame->width;
    int height = frame->height;
    const uint8_t* yData = frame->data[0];
    int yLineSize = frame->linesize[0];
    const uint8_t* uData = frame->data[1];
    int uLineSize = frame->linesize[1];
    const uint8_t* vData = frame->data[2];
    int vLineSize = frame->linesize[2];

    if(!yData || !uData || !vData) return;

    if(frameWidth_ != width || frameHeight_ != height) {
        delete[] yPlane; delete[] uPlane; delete[] vPlane;
        frameWidth_ = width; frameHeight_ = height;
        yPlane = new uint8_t[width*height];
        uPlane = new uint8_t[width*height/4];
        vPlane = new uint8_t[width*height/4];
    }

    for(int i=0; i<height; i++)
        memcpy(yPlane + i*width, yData + i*yLineSize, width);

    for(int i=0; i<height/2; i++) {
        memcpy(uPlane + i*width/2, uData + i*uLineSize, width/2);
        memcpy(vPlane + i*width/2, vData + i*vLineSize, width/2);
    }

    update(); // 触发 paintGL
    av_frame_free(&frame);
}
