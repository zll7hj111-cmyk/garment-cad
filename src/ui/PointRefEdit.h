#pragma once

#include <QLineEdit>
#include <QUuid>

#include <vector>

namespace cad::param { class ParamDocument; }

namespace cad::ui {

/// A point-reference input widget: displays a point's short tag + name
/// (e.g. "P2·颈点") and lets the user type a P-number to re-target.
///
/// Resolution rules (on Enter):
///   1. Full serial exact match (e.g. "dysssP2") → immediate hit.
///   2. Tag match (e.g. "P2", case-insensitive) across all blocks EXCEPT
///      the excluded block:
///        - 1 match  → applied directly.
///        - N matches → disambiguation dialog (full serials shown).
///        - 0 matches → red flash, text reverts.
///   3. Segment match (§6.6 名称复用): "L#" / full serial / segment name →
///      that segment's START point (its exit direction = segment direction).
///   Esc / focus-loss always reverts to the last resolved display.
class PointRefEdit : public QLineEdit
{
    Q_OBJECT

public:
    explicit PointRefEdit(cad::param::ParamDocument* doc, QWidget* parent = nullptr);

    /// Points belonging to this block are excluded from search results.
    void setExcludeBlock(const QUuid& blockId) { m_excludeBlockId = blockId; }

    /// 仅限本块内的点 (对齐点输入用, 2026-09 设计修正): 搜索/名称匹配只在本
    /// 块点集内进行, 线段匹配整体跳过 —— 对齐点是"本线段的哪个端点", 不跨块。
    void setRestrictToBlock(const QUuid& blockId) { m_restrictBlockId = blockId; }

    /// Show the given point in display mode. Null pointId → placeholder state.
    void setPoint(const QUuid& blockId, const QUuid& pointId);

    /// Clear to empty / placeholder state (no resolved point).
    void clearPoint();

    /// 自动态灰显回显 (§6.4): 值来自模型自动跟随, 文字转灰提示"非手填"。
    /// 用户一输入即走正常色 (编辑态 = 自定义意图)。
    void setAutoEcho(bool on);

    /// 是否处于自动态灰显回显 (2026-09 设计修正: 自动回填的点2/对齐点不算
    /// 用户意图, 引用卡的 refresh 预填落库须跳过 autoEcho 字段)。
    [[nodiscard]] bool isAutoEcho() const { return m_autoEcho; }

    [[nodiscard]] QUuid resolvedBlockId() const { return m_blockId; }
    [[nodiscard]] QUuid resolvedPointId() const { return m_pointId; }

signals:
    /// Emitted when the user successfully resolves a new point reference.
    void pointResolved(const QUuid& blockId, const QUuid& pointId);

protected:
    void focusInEvent(QFocusEvent* e) override;
    void focusOutEvent(QFocusEvent* e) override;
    void keyPressEvent(QKeyEvent* e) override;

private:
    struct Match {
        QUuid blockId;
        QUuid pointId;
        QString serial;    ///< Full serial (e.g. "dysssP2").
        QString pointName;
        QString segLabel;  ///< Owning segment tag·name (for disambiguation).
    };

    void commitInput();
    void revertDisplay();
    void applyMatch(const Match& m);
    void flashError();
    /// Build the display text for the current resolved point.
    [[nodiscard]] QString displayTextFor(const QUuid& blockId, const QUuid& pointId) const;
    /// Search the document for points matching the input text.
    [[nodiscard]] std::vector<Match> findMatches(const QString& text) const;

    cad::param::ParamDocument* m_doc = nullptr;
    QUuid m_excludeBlockId;
    QUuid m_restrictBlockId;   ///< 非空 = 仅本块内点可解析 (对齐点, 2026-09).
    QUuid m_blockId;
    QUuid m_pointId;
    QString m_displayText;  ///< Text shown in display (non-editing) mode.
    bool m_autoEcho = false;  ///< 自动态灰显回显 (§6.4).
};

} // namespace cad::ui
