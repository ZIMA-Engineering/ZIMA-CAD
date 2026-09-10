#include "drawing_window.hpp"
#include "application_settings.hpp"

#include <QApplication>

int verify_drawing_ui();
int verify_show_erase_ui();
int verify_measurement_dimension_ui();

int main(int argc, char* argv[]) {
    QApplication application(argc, argv);
    zima::app::apply_application_translations(
        application, zima::app::ApplicationSettings::load());
    if (application.arguments().contains("--verify-show-erase")) return verify_show_erase_ui();
    if (application.arguments().contains("--verify-measurements")) return verify_measurement_dimension_ui();
    if (application.arguments().contains("--verify-ui")) return verify_drawing_ui();
    zima::app::DrawingWindow window;
    window.show();
    return application.exec();
}
