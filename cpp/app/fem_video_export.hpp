#pragma once
#include <QImage>
#include <QString>
#include <functional>

namespace zima::app {
// Export only: no native document state or geometry is written by this encoder.
void write_fem_video(const QString& path,double seconds,
    const std::function<QImage(double)>& frame,
    const std::function<void(int,int)>& progress = {});
}
