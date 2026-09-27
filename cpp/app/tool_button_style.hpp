#pragma once
#include <QString>
namespace zima::app {
inline QString command_button_style() {
    return QStringLiteral(
        "QToolButton:hover:enabled {background:palette(midlight);border:1px solid palette(mid);border-radius:2px;}"
        "QToolButton:pressed:enabled {background:palette(mid);border:1px solid palette(dark);border-radius:2px;}"
        "QToolButton:checked, QToolButton[zimaCommandActive=\"true\"] {"
        "background:#00D1FF;color:#102027;border:1px solid #008DAA;border-radius:2px;}");
}
}
