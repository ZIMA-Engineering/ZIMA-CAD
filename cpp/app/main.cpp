#include "main_window.hpp"
#include "application_settings.hpp"

#include <QApplication>
#include <QSurfaceFormat>

int main(int argc, char* argv[]) {
    QSurfaceFormat format;
    format.setRenderableType(QSurfaceFormat::OpenGL);
    format.setVersion(3, 3);
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setDepthBufferSize(24);
    format.setSamples(4);
    QSurfaceFormat::setDefaultFormat(format);
    QApplication application(argc, argv);
    application.setApplicationName("ZIMA-CAD");
    const auto settings = zima::app::ApplicationSettings::load();
    zima::app::apply_application_translations(application, settings);
    zima::app::apply_application_font(application, settings);
    zima::app::MainWindow window;
    window.show();
    return application.exec();
}
