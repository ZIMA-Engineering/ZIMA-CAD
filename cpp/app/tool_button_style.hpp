#pragma once
#include <QString>
namespace zima::app {
inline QString command_button_style() {
    return QStringLiteral(
        "QToolButton { margin:1px; padding:3px; border:1px solid transparent; border-radius:5px; }"
        "QToolButton:checked:enabled, QToolButton[zimaCommandActive=\"true\"] { background-color:#00D1FF; color:#102027; border:1px solid #00D1FF; }"
        "QToolButton:hover:enabled, QToolButton:checked:hover:enabled, QToolButton[zimaCommandActive=\"true\"]:hover { background-color:#4dd811; color:#102027; border:1px solid #4dd811; }"
        "QToolButton:pressed:enabled { background-color:#4dd811; color:#102027; border:1px solid #4dd811;"
        " padding-left:4px; padding-top:4px; padding-right:2px; padding-bottom:2px; }"
        "QToolButton:disabled { color:rgba(255,255,255,70); }");
}
}
