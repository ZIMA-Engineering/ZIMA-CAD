#pragma once
#include <QString>
namespace zima::app {
inline QString command_button_style() {
    return QStringLiteral(
        "QToolButton { margin:1px; padding:3px; border:1px solid transparent; border-radius:5px; }"
        "QToolButton:hover:enabled { background-color:rgba(77,216,17,72); color:#fff; border:1px solid rgba(128,170,26,190); }"
        "QToolButton:pressed:enabled { background-color:rgba(77,216,17,175); color:#fff; border:1px solid #9BCC32;"
        " padding-left:4px; padding-top:4px; padding-right:2px; padding-bottom:2px; }"
        "QToolButton:disabled { color:rgba(255,255,255,70); }");
}
}
