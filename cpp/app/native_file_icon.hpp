#pragma once
#include "resource_icon.hpp"
#include <QFileInfo>
#include <QRegularExpression>

namespace zima::app {
inline QIcon native_file_icon(const QFileInfo& info) {
    if(info.isDir())return {};
    static const QRegularExpression extension(R"(\.(prtz|asmz|drwz|frmz|tblz|symz)(?:\.\d+)?$)",QRegularExpression::CaseInsensitiveOption);
    const auto match=extension.match(info.fileName());if(!match.hasMatch())return {};
    const auto suffix=match.captured(1).toLower();
    if(suffix=="prtz")return resource_icon("part");
    if(suffix=="asmz")return resource_icon("assembly");
    if(suffix=="drwz")return resource_icon("drawing");
    if(suffix=="frmz")return resource_icon("drawing-format");
    if(suffix=="tblz")return resource_icon("title-block");
    return resource_icon("symbol");
}
} // namespace zima::app
