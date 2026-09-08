#include "drawing_window.hpp"

#include <QApplication>

int verify_drawing_ui();

int main(int argc, char* argv[]) {
    QApplication application(argc, argv);
    if (application.arguments().contains("--verify-ui")) return verify_drawing_ui();
    zima::app::DrawingWindow window;
    window.show();
    return application.exec();
}
