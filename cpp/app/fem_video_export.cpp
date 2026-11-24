#include "fem_video_export.hpp"
#include <QEventLoop>
#include <QFileInfo>
#include <QMediaCaptureSession>
#include <QMediaFormat>
#include <QMediaRecorder>
#include <QSaveFile>
#include <QTemporaryFile>
#include <QUrl>
#include <QTimer>
#include <QVideoFrame>
#include <QVideoFrameInput>
#include <cmath>
#include <stdexcept>

namespace zima::app {
void write_fem_video(const QString& path,double seconds,const std::function<QImage(double)>& frame,
        const std::function<void(int,int)>& progress) {
    if(QFileInfo(path).suffix().compare("mp4",Qt::CaseInsensitive)!=0)
        throw std::runtime_error("Choose an MP4 file for the video export.");
    if(!std::isfinite(seconds)||seconds<.3||seconds>30)
        throw std::runtime_error("Animation duration must be between 0.3 and 30 seconds.");
    QMediaFormat format(QMediaFormat::MPEG4);format.setVideoCodec(QMediaFormat::VideoCodec::H264);
    if(!format.isSupported(QMediaFormat::Encode))throw std::runtime_error("MP4 video encoding is unavailable.");
    QTemporaryFile temporary(QFileInfo(path).absolutePath()+"/.zima-video-XXXXXX.mp4");
    if(!temporary.open())throw std::runtime_error("Cannot create the video file.");
    const auto temporary_path=temporary.fileName();temporary.close();
    constexpr int fps=30;
    const int frames=std::max(1,qRound(seconds*fps));
    QEventLoop loop;QTimer timeout;timeout.setSingleShot(true);
    QMediaCaptureSession session;QVideoFrameInput input;QMediaRecorder recorder;
    session.setVideoFrameInput(&input);session.setRecorder(&recorder);
    recorder.setMediaFormat(format);recorder.setVideoFrameRate(fps);
    recorder.setQuality(QMediaRecorder::HighQuality);recorder.setAutoStop(true);
    recorder.setOutputLocation(QUrl::fromLocalFile(temporary_path));
    QString error;int index=0;bool started=false,finished=false,end_sent=false;
    QVideoFrame pending;QSize size;
    const auto fail=[&](const QString& message){error=message;recorder.stop();loop.quit();};
    std::function<void()> send;
    send=[&] {
        if(!error.isEmpty()||end_sent||recorder.recorderState()!=QMediaRecorder::RecordingState)return;
        try {
            if(index==frames) {end_sent=input.sendVideoFrame({});return;}
            if(!pending.isValid()) {
                auto image=frame(double(index)/frames);
                if(image.isNull())throw std::runtime_error("Cannot capture the video frame.");
                if(size.isEmpty()) {
                    size=image.size().scaled(QSize(1920,1080),Qt::KeepAspectRatio);
                    // H.264 encoders require even frame dimensions.
                    size.setWidth(std::max(2,size.width()/2*2));size.setHeight(std::max(2,size.height()/2*2));
                }
                pending=QVideoFrame(image.scaled(size,Qt::IgnoreAspectRatio,Qt::SmoothTransformation).convertToFormat(QImage::Format_RGBA8888));
                pending.setStartTime(qint64(index)*1000000/fps);
                pending.setEndTime(qint64(index+1)*1000000/fps);
            }
            if(input.sendVideoFrame(pending)) {
                pending={};++index;if(progress)progress(index,frames);
                QTimer::singleShot(0,&loop,send);
            }
        }catch(const std::exception& exception){fail(QObject::tr(exception.what()));}
    };
    QObject::connect(&input,&QVideoFrameInput::readyToSendVideoFrame,&loop,send,Qt::QueuedConnection);
    QObject::connect(&recorder,&QMediaRecorder::recorderStateChanged,&loop,[&](auto state) {
        if(state==QMediaRecorder::RecordingState){started=true;QTimer::singleShot(0,&loop,send);}
        else if(started&&state==QMediaRecorder::StoppedState){finished=true;loop.quit();}
    });
    QObject::connect(&recorder,&QMediaRecorder::errorOccurred,&loop,[&](auto,const QString& detail) {
        fail(QObject::tr("Video export failed: %1").arg(detail));
    });
    QObject::connect(&timeout,&QTimer::timeout,&loop,[&]{fail(QObject::tr("Video export timed out."));});
    timeout.start(180000);recorder.record();
    if(error.isEmpty()&&!finished)loop.exec();
    recorder.stop();
    if(!error.isEmpty())throw std::runtime_error(error.toStdString());
    if(!finished||index!=frames||!end_sent||QFileInfo(temporary_path).size()==0)
        throw std::runtime_error("Video export did not finish.");
    QFile encoded(temporary_path);QSaveFile output(path);output.setDirectWriteFallback(false);
    if(!encoded.open(QIODevice::ReadOnly)||!output.open(QIODevice::WriteOnly))
        throw std::runtime_error("Cannot save the video file.");
    while(!encoded.atEnd()) {
        const auto bytes=encoded.read(1024*1024);
        if(bytes.isEmpty()||output.write(bytes)!=bytes.size())throw std::runtime_error("Cannot save the video file.");
    }
    if(!output.commit())throw std::runtime_error("Cannot save the video file.");
}
}
