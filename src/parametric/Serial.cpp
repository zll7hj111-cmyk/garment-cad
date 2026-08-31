#include "Serial.h"

#include <QRandomGenerator>

namespace cad::param::Serial {

namespace {
constexpr int kPrefixLen = 5;
const char kCharset[] = "abcdefghijklmnopqrstuvwxyz0123456789";
constexpr int kCharsetLen = sizeof(kCharset) - 1;
} // namespace

QString randomPrefix(int len)
{
    QString s;
    s.reserve(len);
    auto* gen = QRandomGenerator::global();
    for (int i = 0; i < len; ++i)
        s.append(QLatin1Char(kCharset[gen->bounded(kCharsetLen)]));
    return s;
}

QString make(const QString& prefix, QChar letter, int seq)
{
    return prefix + letter + QString::number(seq);
}

QString prefix(const QString& serial)
{
    return serial.left(kPrefixLen);
}

QString tag(const QString& serial)
{
    return serial.mid(kPrefixLen);
}

QString toHtml(const QString& serial)
{
    if (serial.isEmpty())
        return QString();
    const QString pfx = prefix(serial);
    const QString tg  = tag(serial);
    // parametric 层富文本固定色——分层铁律禁止引用 ui 主题 token
    return QStringLiteral("<span style=\"color:#9a9a9a;\">%1</span><span style=\"color:#d40000; font-weight:bold;\">%2</span>")  // color-allow: 双固定色（灰前缀+红串号, 串号语义色不随主题）
        .arg(pfx.toHtmlEscaped(), tg.toHtmlEscaped());
}

} // namespace cad::param::Serial
