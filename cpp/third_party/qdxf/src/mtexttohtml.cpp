/*
  ZIMA-libqdxf
  http://www.zima-engineering.cz/projekty/ZIMA-libqdxf

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "mtexttohtml.h"
#include <functional>
#include <QStringList>
#include <QVector>

namespace {

struct TextFormat
{
    bool underline = false;
    bool overline = false;
    bool strikeOut = false;
    bool bold = false;
    bool italic = false;
    QString fontFamily;
};

QString cssForFormat(const TextFormat &format)
{
    QStringList styles;
    QStringList decorations;

    if (format.underline)
        decorations << QStringLiteral("underline");
    if (format.overline)
        decorations << QStringLiteral("overline");
    if (format.strikeOut)
        decorations << QStringLiteral("line-through");

    if (!decorations.isEmpty())
        styles << QStringLiteral("text-decoration:%1").arg(decorations.join(QLatin1Char(' ')));
    if (format.bold)
        styles << QStringLiteral("font-weight:600");
    if (format.italic)
        styles << QStringLiteral("font-style:italic");
    if (!format.fontFamily.isEmpty())
    {
        QString family = format.fontFamily;
        family.replace(QLatin1Char('\''), QStringLiteral("\\'"));
        styles << QStringLiteral("font-family:'%1'").arg(family.toHtmlEscaped());
    }

    return styles.join(QLatin1Char(';'));
}

QString readableStackText(QString stackText)
{
    stackText.replace(QLatin1Char('#'), QLatin1Char('/'));
    stackText.replace(QLatin1Char('^'), QLatin1Char('/'));
    return stackText;
}

void applyFontTag(const QString &tag, TextFormat &format)
{
    const QStringList parts = tag.mid(1).split(QLatin1Char('|'));
    if (!parts.isEmpty())
        format.fontFamily = parts.first().trimmed();

    for (const QString &part : parts)
    {
        if (part.startsWith(QLatin1Char('b'), Qt::CaseInsensitive))
            format.bold = part.mid(1).toInt() != 0;
        else if (part.startsWith(QLatin1Char('i'), Qt::CaseInsensitive))
            format.italic = part.mid(1).toInt() != 0;
    }
}

} // namespace

QString MTextToHTML::convert(QString mtext)
{
    QString html;
    TextFormat format;
    QVector<TextFormat> formatStack;
    bool spanOpen = false;

    auto closeSpan = [&html, &spanOpen]() {
        if (spanOpen)
        {
            html += QStringLiteral("</span>");
            spanOpen = false;
        }
    };

    auto ensureSpan = [&html, &format, &spanOpen]() {
        if (spanOpen)
            return;

        const QString style = cssForFormat(format);
        if (!style.isEmpty())
        {
            html += QStringLiteral("<span style=\"%1\">").arg(style);
            spanOpen = true;
        }
    };

    auto appendText = [&html, &ensureSpan](const QString &text) {
        ensureSpan();
        html += text.toHtmlEscaped();
    };

    auto changeFormat = [&closeSpan](TextFormat &format,
                                     const std::function<void(TextFormat &)> &change) {
        closeSpan();
        change(format);
    };

    for (int i = 0; i < mtext.length(); ++i)
    {
        const QChar c = mtext.at(i);

        if (c == QLatin1Char('{'))
        {
            formatStack.append(format);
            continue;
        }
        if (c == QLatin1Char('}'))
        {
            if (!formatStack.isEmpty())
            {
                const TextFormat parentFormat = formatStack.takeLast();
                changeFormat(format, [&parentFormat](TextFormat &f) { f = parentFormat; });
            }
            continue;
        }

        if (c != QLatin1Char('\\'))
        {
            appendText(QString(c));
            continue;
        }

        if (++i >= mtext.length())
        {
            appendText(QStringLiteral("\\"));
            break;
        }

        const QChar command = mtext.at(i);
        switch (command.toLatin1())
        {
        case 'L':
            changeFormat(format, [](TextFormat &f) { f.underline = true; });
            break;
        case 'l':
            changeFormat(format, [](TextFormat &f) { f.underline = false; });
            break;
        case 'O':
            changeFormat(format, [](TextFormat &f) { f.overline = true; });
            break;
        case 'o':
            changeFormat(format, [](TextFormat &f) { f.overline = false; });
            break;
        case 'K':
            changeFormat(format, [](TextFormat &f) { f.strikeOut = true; });
            break;
        case 'k':
            changeFormat(format, [](TextFormat &f) { f.strikeOut = false; });
            break;
        case 'P':
        case 'X':
            closeSpan();
            html += QStringLiteral("<br/>");
            break;
        case '~':
            appendText(QStringLiteral(" "));
            break;
        case '\\':
            appendText(QStringLiteral("\\"));
            break;
        case '{':
            appendText(QStringLiteral("{"));
            break;
        case '}':
            appendText(QStringLiteral("}"));
            break;
        default:
        {
            const int end = mtext.indexOf(QLatin1Char(';'), i);
            if (end < 0)
                break;

            const QString tag = mtext.mid(i, end - i);
            if (command == QLatin1Char('f') || command == QLatin1Char('F'))
                changeFormat(format, [&tag](TextFormat &f) { applyFontTag(tag, f); });
            else if (command == QLatin1Char('S'))
                appendText(readableStackText(tag.mid(1)));

            i = end;
            break;
        }
        }
    }

    closeSpan();
    return html;
}
