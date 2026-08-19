#include "drawing_window.hpp"

#include <QApplication>

int main(int argc, char* argv[]) {
    QApplication application(argc, argv);
    zima::app::DrawingWindow window;
    window.show();
    return application.exec();
}
