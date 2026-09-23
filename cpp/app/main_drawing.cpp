#include "drawing_window.hpp"
#include "application_settings.hpp"

#include <QApplication>
#include <QSurfaceFormat>

int verify_drawing_ui();
int verify_drawing_details_ui();
int verify_drawing_breaks_ui();
int verify_drawing_source_picker();
int verify_show_erase_ui();
int verify_measurement_dimension_ui();
int verify_drawing_balloon_ui();

int main(int argc, char* argv[]) {
    // Exercise the same GPU canvas format as the product application.
    QSurfaceFormat format;format.setRenderableType(QSurfaceFormat::OpenGL);
    format.setVersion(3,3);format.setProfile(QSurfaceFormat::CoreProfile);
    format.setDepthBufferSize(24);format.setSamples(4);QSurfaceFormat::setDefaultFormat(format);
    QApplication application(argc, argv);
    if (application.arguments().contains("--verify-details")) return verify_drawing_details_ui();
    if (application.arguments().contains("--verify-source-picker")) return verify_drawing_source_picker();
    zima::app::apply_application_translations(
        application, zima::app::ApplicationSettings::load());
    if (application.arguments().contains("--verify-show-erase")) return verify_show_erase_ui();
    if (application.arguments().contains("--verify-measurements")) return verify_measurement_dimension_ui();
    if (application.arguments().contains("--verify-balloons")) return verify_drawing_balloon_ui();
    if (application.arguments().contains("--verify-view-controls")) {
        qputenv("ZIMA_VERIFY_VIEW_CONTROLS_ONLY","1");return verify_drawing_ui();
    }
    if (application.arguments().contains("--verify-breaks")) return verify_drawing_breaks_ui();
    if (application.arguments().contains("--verify-ui")) return verify_drawing_ui();
    zima::app::DrawingWindow window;
    window.show();
    return application.exec();
}
