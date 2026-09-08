#pragma once

#include <QUuid>
#include <vector>

#include "parametric/Resolver.h"

namespace cad::param {

/// 追加一条诊断，但同一 (kind, attachment) 组合只记一次（resolve 循环会
/// 多次访问同一条 attachment）。唯一实现（2026-12 审计 P1-8 收口，原
/// Resolver.cpp / ResolverAttachment.cpp 各有一份逐字重复的匿名命名空间副本）。
inline void appendDiagnostic(std::vector<ResolveDiagnostic>* diagnostics,
                             ResolveDiagnostic::Kind kind, const QUuid& attachmentId)
{
    if (!diagnostics) return;
    for (const auto& d : *diagnostics)
        if (d.kind == kind && d.attachmentId == attachmentId) return;
    diagnostics->push_back({kind, attachmentId});
}

} // namespace cad::param
