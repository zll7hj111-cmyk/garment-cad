#pragma once

// 跨层/同层交点重解现场调试 (用户 2026-08-24 现场会话交点不更新复现):
// env GCAD_DEBUG_INTERSECT=1 时, 每次交点求值 (命中/未命中/起点缺失) 追一行
// 到 E:/garment-cad/gcad_intersect_debug.log; 未设 env 零开销。

#include <QString>
#include <QtGlobal>

#include <cstdio>

namespace cad::param::idbg {

inline bool enabled()
{
    static const bool on = qEnvironmentVariable("GCAD_DEBUG_INTERSECT") == QLatin1String("1");
    return on;
}

inline void log(const QString& line)
{
    if (!enabled()) return;
    static FILE* fp = nullptr;
    if (!fp) {
        fp = _wfopen(L"E:/garment-cad/gcad_intersect_debug.log", L"ab");   // plain byte append (UTF-8 bytes)
        if (!fp) return;
        std::fputs("\n=== session ===\n", fp);
    }
    const QByteArray utf8 = line.toUtf8();
    std::fwrite(utf8.constData(), 1, utf8.size(), fp);
    std::fputc('\n', fp);
    std::fflush(fp);
}

} // namespace cad::param::idbg
