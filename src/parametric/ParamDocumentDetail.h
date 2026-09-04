#pragma once

namespace cad::param {

/// Predicted consequences of removing one block — every counter mirrors
/// one cascade branch of removeBlock() (删除影响报告). Kept in sync with
/// the cleanup logic; advisory only (the deletion itself is unconditional).
struct DeleteImpact {
    int attachmentsRemoved   = 0;  ///< 连接数（attachment）。
    int bridgesReleased      = 0;  ///< 释放为独立线段的桥接线（保留几何）。
    int intersectionsFrozen  = 0;  ///< 冻结在原位置的交点（射线原点消失）。
    int intersectionsAimCleared = 0; ///< 交点回退为固定角度的指向点（指向点被删）。
    int linkedFrozen         = 0;  ///< 长度引用固化为数值的段/点（引用对象被删）。
    int linkedVarsRemoved    = 0;  ///< 删除的关联长度变量。
    int measureVarsRemoved   = 0;  ///< 删除的测量变量（不可恢复）。
    int angleVarsRemoved     = 0;  ///< 删除的角度测量变量（不可恢复）。
    int formulasBroken       = 0;  ///< 引用被删测量名的公式（将失效报错）。
    int dartLinesDegraded    = 0;  ///< 失去起点/偏移点而降级为普通线的省道线。

    [[nodiscard]] bool hasImpact() const
    {
        return attachmentsRemoved > 0 || bridgesReleased > 0 ||
               intersectionsFrozen > 0 || intersectionsAimCleared > 0 ||
               linkedFrozen > 0 ||
               linkedVarsRemoved > 0 || measureVarsRemoved > 0 ||
               angleVarsRemoved > 0 || formulasBroken > 0 ||
               dartLinesDegraded > 0;
    }
    DeleteImpact& operator+=(const DeleteImpact& o)
    {
        attachmentsRemoved  += o.attachmentsRemoved;
        bridgesReleased     += o.bridgesReleased;
        intersectionsFrozen += o.intersectionsFrozen;
        intersectionsAimCleared += o.intersectionsAimCleared;
        linkedFrozen        += o.linkedFrozen;
        linkedVarsRemoved   += o.linkedVarsRemoved;
        measureVarsRemoved  += o.measureVarsRemoved;
        angleVarsRemoved    += o.angleVarsRemoved;
        formulasBroken      += o.formulasBroken;
        dartLinesDegraded   += o.dartLinesDegraded;
        return *this;
    }
};

} // namespace cad::param
