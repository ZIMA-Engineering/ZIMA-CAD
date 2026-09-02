#include "main_window.hpp"

#include <QApplication>
#include <QFontDatabase>
#include <QSurfaceFormat>

namespace {

void use_iso_application_font(QApplication& application) {
    const int font_id = QFontDatabase::addApplicationFont(
        QStringLiteral(":/zima/fonts/osifont-lgpl3fe.ttf"));
    if (font_id < 0) return;
    const auto families = QFontDatabase::applicationFontFamilies(font_id);
    if (families.empty()) return;
    auto font = application.font();
    font.setFamily(families.front());
    application.setFont(font);
}

}  // namespace

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
    use_iso_application_font(application);
    zima::app::MainWindow window;
    window.show();
    return application.exec();
}
