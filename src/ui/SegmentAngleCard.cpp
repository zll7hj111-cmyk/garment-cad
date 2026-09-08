#include "ui/SegmentAngleCard.h"

#include <algorithm>
#include <cmath>
#include <optional>

#include <QHBoxLayout>
#include <QSignalBlocker>
#include <QVBoxLayout>

#include "ElaText.h"
#include "ElaLineEdit.h"

#include "parametric/ParamDocument.h"
#include "parametric/Block.h"
#include "parametric/Attachment.h"
#include "parametric/ConditionEngine.h"
#include "parametric/FollowerAngle.h"
#include "document/commands/AttachmentAngleCommands.h"
#include "document/commands/SegmentPropertyCommands.h"
#include "geometry/Units.h"
#include "geometry/Angle.h"
#include "ui/Theme.h"
#include "ui/TooltipFormatter.h"
#include "ui/FormScaffold.h"
#include "geometry/Epsilon.h"

namespace cad::ui {

namespace {
constexpr int kLabelW = 64;   ///< 标签列定宽 (2026-12 去卡框化: 短词列).
constexpr int kFieldH = 30;   ///< 2026-xx 紧凑化 (35→30, 与状态栏对齐).

/// 计算线段起止两点构成的世界绝对角度（0~360° 逆时针为正）.
std::optional<double> worldDegOfSegment(const cad::param::Block* block,
                                        const cad::param::Segment* seg)
{
    if (!block || !seg) return std::nullopt;
    const auto* sp = block->findPoint(seg->startPointId);
    const auto* ep = block->findPoint(seg->endPointId);
    if (!sp || !ep || !sp->resolved || !ep->resolved) return std::nullopt;
    const cad::geo::Vec2 w1 = block->transform.toWorld(sp->resolvedPos);
    const cad::geo::Vec2 w2 = block->transform.toWorld(ep->resolvedPos);
    return cad::geo::normalizeDeg360(
        cad::geo::radToDeg(std::atan2(w2.y - w1.y, w2.x - w1.x)));
}

/// 计算自由线段的世界角度或正交偏置线段的中心基准轴角度.
std::optional<double> worldOrNominalDegOfSegment(const cad::param::Block* block,
                                                 const cad::param::Segment* seg)
{
    if (!block || !seg) return std::nullopt;
    const auto* ep = block->findPoint(seg->endPointId);
    if (ep && ep->constraint == cad::param::PointConstraint::OrthoOffset) {
        const double rotDeg = cad::geo::radToDeg(block->transform.rotation);
        return cad::geo::normalizeDeg360(ep->angle + rotDeg);
    }
    return worldDegOfSegment(block, seg);
}

/// 寻找决定线段角度属性的驱动点（优先使用带公式的端点，默认终点）.
const cad::param::ParamPoint* findDrivenAnglePoint(const cad::param::Block* block,
                                                   const cad::param::Segment* seg)
{
    if (!block || !seg) return nullptr;
    const auto* ep = block->findPoint(seg->endPointId);
    const auto* sp = block->findPoint(seg->startPointId);
    return (ep && !ep->angleFormula.isEmpty()) ? ep
        : ((sp && !sp->angleFormula.isEmpty()) ? sp : ep);
}

/// 求值驱动点的角度公式并返回回显文本（如 "= 45.00°"）. 若无公式或求值失败返回空.
QString evaluatedFollowValueText(const cad::param::Block* block,
                                 const cad::param::ParamPoint* driven,
                                 const cad::param::ParamDocument* doc)
{
    if (!driven || driven->angleFormula.isEmpty() || !doc) return {};
    auto r = cad::param::ConditionEngine::evaluate(
        driven->angleFormula, doc->parameters(), {});
    if (!r.ok) return {};
    // 回显域规则 (审计 UI-P0-2 复核): 本分支显示的是**独立线段的绝对世界
    // 方向** (点角公式 + 块旋转), 属世界域 → 0..360; 附件分支显示的是**跟随
    // 折角**, 属折角域 → (−180,180]。两者是不同物理量, 各自与所属输入框同域。
    const double rotDeg = block ? cad::geo::radToDeg(block->transform.rotation) : 0.0;
    const double deg = cad::geo::normalizeDeg360(r.value + rotDeg);
    return QString::fromUtf8("= %1°").arg(cad::geo::Units::formatDegValue(deg));
}

/// 三模态（角度/弧长/开度）控件配置表.
struct ModeProfile {
    QString symbol;
    QString caption;
    QString placeholder;
};

ModeProfile modeProfile(cad::param::RotationMode mode)
{
    switch (mode) {
    case cad::param::RotationMode::ArcLength:
        return {QStringLiteral("⌒"), QString::fromUtf8("弧长"), cad::ui::kPlaceholderCmOrFormula};
    case cad::param::RotationMode::ChordLength:
        return {QStringLiteral("↔"), QString::fromUtf8("开度"), cad::ui::kPlaceholderCmOrFormula};
    case cad::param::RotationMode::Angle:
    default:
        return {QStringLiteral("∠"), QString::fromUtf8("跟随角"), cad::ui::kPlaceholderAngleOrFormula};
    }
}

/// 读取附件对应模式的数值与公式.
struct ModeValue {
    double value = 0.0;
    QString formula;
};

ModeValue getAttachmentModeValue(const cad::param::Attachment& att)
{
    switch (att.rotationMode) {
    case cad::param::RotationMode::ArcLength:
        return {att.arcLength, att.arcLengthFormula};
    case cad::param::RotationMode::ChordLength:
        return {att.chordLength, att.chordLengthFormula};
    case cad::param::RotationMode::Angle:
    default:
        return {att.followerAngle, att.followerAngleFormula};
    }
}

/// 计算附件在当前模式下表达的实际角度（度制; 角度模式返回折角显示域
/// (−180,180], 弧长/开度返回 0..360 等效角）.
double attachmentEffectiveAngleDeg(const cad::param::Attachment& att,
                                   const cad::param::ParamDocument* doc,
                                   double radius)
{
    if (att.rotationMode == cad::param::RotationMode::ArcLength) {
        double arcMm = att.arcLength;
        if (doc)
            (void)cad::param::ConditionEngine::evaluateLengthMm(
                att.arcLengthFormula, doc->parameters(), doc->conditions(), arcMm);
        const double alphaDeg = (radius > cad::geo::kGeomEps) ? cad::geo::arcMmToDeg(arcMm, radius) : 0.0;
        return cad::geo::normalizeDeg360(alphaDeg);
    }
    if (att.rotationMode == cad::param::RotationMode::ChordLength) {
        double chordMm = att.chordLength;
        if (doc)
            (void)cad::param::ConditionEngine::evaluateLengthMm(
                att.chordLengthFormula, doc->parameters(), doc->conditions(), chordMm);
        const double alphaDeg = (radius > cad::geo::kGeomEps) ? cad::geo::chordMmToDeg(chordMm, radius) : 0.0;
        return cad::geo::normalizeDeg360(alphaDeg);
    }
    double constDeg = att.followerAngle;
    if (!att.followerAngleFormula.isEmpty() && doc) {
        auto r = cad::param::ConditionEngine::evaluate(
            att.followerAngleFormula, doc->parameters(), doc->conditions());
        if (r.ok) constDeg = r.value;
    }
    // 显示域统一折角 (2026-12 审计 P0-2): 周期安全, 与输入框同域。
    return cad::param::followerAngleToDisplay(constDeg);
}

/// 格式化附件跟随值（用于卡片回显，若无公式则返回空）.
QString formatAttachmentFormulaFollowValue(const cad::param::Attachment& att,
                                           const cad::param::ParamDocument* doc,
                                           double radius)
{
    if (!doc || getAttachmentModeValue(att).formula.isEmpty()) return {};
    // 2026-12 审计 P0-2 收口: 弧长/开度直接求值并原样显示 (cm), 不再经
    // degToArcMm(foldDeg) 往返（等效角 >180° 时往返会显示成负值）;
    // 角度走折角显示域。
    if (att.rotationMode == cad::param::RotationMode::ArcLength) {
        double arcMm = att.arcLength;
        (void)cad::param::ConditionEngine::evaluateLengthMm(
            att.arcLengthFormula, doc->parameters(), doc->conditions(), arcMm);
        return QString::fromUtf8("= %1").arg(
            cad::geo::Units::formatLength(arcMm));
    }
    if (att.rotationMode == cad::param::RotationMode::ChordLength) {
        double chordMm = att.chordLength;
        (void)cad::param::ConditionEngine::evaluateLengthMm(
            att.chordLengthFormula, doc->parameters(), doc->conditions(), chordMm);
        return QString::fromUtf8("= %1").arg(
            cad::geo::Units::formatLength(chordMm));
    }
    const double foldDeg = cad::param::followerAngleToDisplay(
        attachmentEffectiveAngleDeg(att, doc, radius));
    return QString::fromUtf8("= %1°").arg(cad::geo::Units::formatDegValue(foldDeg));
}

/// 格式化附件数值输入框显示内容（无公式时使用）.
/// 2026-12 审计 P0-2 收口: 弧长/开度按存储值原样显示（cm）——此前经
/// arcMmToDeg→normalizeDeg180→degToArcMm 往返，等效角 >180°（弧长 > πr）
/// 时输入框会显示成负值，与用户输入/存储值不同数。
QString formatAttachmentDisplayValue(cad::param::RotationMode mode, double value, double radius)
{
    Q_UNUSED(radius);
    if (mode == cad::param::RotationMode::ArcLength
        || mode == cad::param::RotationMode::ChordLength) {
        return cad::geo::Units::formatCm(value);
    }
    return cad::geo::Units::formatDegValue(cad::param::followerAngleToDisplay(value));
}

} // namespace

SegmentAngleCard::SegmentAngleCard(cad::param::ParamDocument* doc, QWidget* parent)
    : QWidget(parent)
    , m_doc(doc)
{
    // 纯行组: 无边框/无底色 (与相邻行一致, 不做"卡中卡")。
    setStyleSheet(QStringLiteral(
        "SegmentAngleCard { background: transparent; border: none; }"));

    auto* col = new QVBoxLayout(this);
    col->setContentsMargins(0, 0, 0, 0);
    col->setSpacing(4);

    // ── 第一行: 角度编辑 (原有行) ──
    auto* row = new QHBoxLayout();
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(6);

    m_lblCaption = new ElaText(QString::fromUtf8("角度"), 11, this);
    m_lblCaption->setFixedWidth(kLabelW);
    row->addWidget(m_lblCaption);
    m_lblFxAngle = new ElaText(
        QStringLiteral("<i style='color:%1;'>fx</i>")
            .arg(cad::ui::Theme::tokens().text2.name()),
        11, this);
    m_lblFxAngle->setVisible(false);
    m_lblFxAngle->setFixedWidth(18);
    row->addWidget(m_lblFxAngle);
    m_editAngle = new ElaLineEdit(this);
    m_editAngle->setFixedWidth(150);
    m_editAngle->setFixedHeight(kFieldH);
    m_editAngle->setStyleSheet(QStringLiteral("font-size: 11px;"));
    m_editAngle->setPlaceholderText(cad::ui::kPlaceholderAngleOrFormula);
    m_editAngle->setToolTip(cad::ui::TooltipFormatter::action(
        QStringLiteral("线段角度"),
        QStringLiteral("自由线：世界绝对角度；跟随线：相对基准线的构造角。逆时针为正，回车确认")));
    row->addWidget(m_editAngle);
    m_lblFollowValue = new ElaText(QString(), 11, this);
    m_lblFollowValue->setObjectName(QStringLiteral("followValueLabel"));
    // 实例样式表必须带 background:transparent —— 否则替换 ElaText 的透明
    // 背景规则, 细字抗锯齿混成灰 (「盖滤镜」, 同分区标题坑)。
    m_lblFollowValue->setStyleSheet(
        QStringLiteral("font-size:11px; background:transparent;"));
    m_lblFollowValue->setVisible(false);
    row->addWidget(m_lblFollowValue);
    m_lblWorldAngle = new ElaText(QString(), 11, this);
    m_lblWorldAngle->setStyleSheet(
        QStringLiteral("font-size:11px; background:transparent;"));
    m_lblWorldAngle->setVisible(false);
    row->addWidget(m_lblWorldAngle);
    m_btnAngleMode = new QPushButton(this);
    m_btnAngleMode->setFixedSize(30, kFieldH);
    m_btnAngleMode->setStyleSheet(cad::ui::chipButtonStyle());
    m_btnAngleMode->setCursor(Qt::PointingHandCursor);
    m_btnAngleMode->setToolTip(cad::ui::TooltipFormatter::action(
        QStringLiteral("切换度数/弧长"),
        QStringLiteral("在角度制（°）与对应基准线弧长制（⌒ mm）之间切换显示与输入模式")));
    row->addWidget(m_btnAngleMode);
    row->addStretch();
    col->addLayout(row);

    // (2026-xx §3) 旧「角度基准 P1→P2」读数行已删 —— 对齐锚点 [P1] 与基准
    // 句式由 SegmentRefCard 承担 (换向由朝向箭头表达)。

    connect(m_editAngle, &QLineEdit::textChanged,
            this, &SegmentAngleCard::onAngleDirty);
    connect(m_editAngle, &QLineEdit::editingFinished,
            this, &SegmentAngleCard::applyAngle);
    connect(m_btnAngleMode, &QPushButton::clicked,
            this, &SegmentAngleCard::onModeToggle);
    connect(m_doc, &cad::param::ParamDocument::resolved,
            this, &SegmentAngleCard::onDocResolved);
}

void SegmentAngleCard::setTarget(const QUuid& blockId, const QUuid& segmentId)
{
    m_blockId = blockId;
    m_segmentId = segmentId;
    populateAngleField();
    refresh();
}

const cad::param::Attachment* SegmentAngleCard::findFollowerAttachment() const
{
    return m_doc ? m_doc->findFollowerAttachmentOf(m_blockId) : nullptr;
}

void SegmentAngleCard::refresh()
{
    if (!m_doc) return;
    const auto* block = m_doc->blocksView().byId(m_blockId);
    const auto* seg = block ? block->findSegment(m_segmentId) : nullptr;
    const auto* att = findFollowerAttachment();
    const bool hasAtt = att != nullptr;

    m_lblFollowValue->setVisible(false);
    m_lblFxAngle->setVisible(false);
    m_lblWorldAngle->setVisible(false);

    // 指向生效/桥接线: 角度由约束决定 → 灰只读 (启用态由下方分支管理)。
    const bool angleGray = (block && !block->endTargetPointId.isNull())
        || (block && block->isBridge);
    m_editAngle->setEnabled(!angleGray);
    m_btnAngleMode->setEnabled(hasAtt && !att->angleIndependent && !angleGray);

    if (!hasAtt || att->angleIndependent) {
        m_btnAngleMode->setText(QStringLiteral("∠"));
        m_lblCaption->setText(hasAtt ? QString::fromUtf8("独立角") : QString::fromUtf8("角度"));
        m_lblCaption->setStyleSheet(QString());
        if (auto d = worldOrNominalDegOfSegment(block, seg)) {
            const QString text = QString::fromUtf8("= 世界角度 %1°")
                .arg(cad::geo::Units::formatDegValue(*d));
            if (m_lblWorldAngle->text() != text)
                m_lblWorldAngle->setText(text);
            m_lblWorldAngle->setVisible(true);
        }
        const auto* driven = findDrivenAnglePoint(block, seg);
        if (driven && !driven->angleFormula.isEmpty()) {
            const QString valText = evaluatedFollowValueText(block, driven, m_doc);
            if (!valText.isEmpty()) {
                m_lblFollowValue->setText(valText);
                m_lblFollowValue->setVisible(true);
            }
            m_lblFxAngle->setVisible(true);
        }
        return;
    }

    const auto prof = modeProfile(att->rotationMode);
    m_btnAngleMode->setText(prof.symbol);
    m_lblCaption->setText(prof.caption);
    m_lblCaption->setStyleSheet(QString());

    const double radius = block ? block->segmentLengthAtPoint(att->fromPointId) : 0.0;
    const QString fv = formatAttachmentFormulaFollowValue(*att, m_doc, radius);
    if (!fv.isEmpty()) {
        m_lblFollowValue->setText(fv);
        m_lblFollowValue->setVisible(true);
        m_lblFxAngle->setVisible(true);
    }
    updateWorldAngleLabel(*att);
}

void SegmentAngleCard::populateAngleField()
{
    if (!m_doc) return;
    auto* block = m_doc->findBlock(m_blockId);
    auto* seg = block ? block->findSegment(m_segmentId) : nullptr;

    const QSignalBlocker sb(m_editAngle);

    m_editAngle->setEnabled(!(block && block->isBridge)
                            && !(block && !block->endTargetPointId.isNull()));
    const auto* att = findFollowerAttachment();

    if ((!att || att->angleIndependent) && block && seg) {
        const auto* driven = findDrivenAnglePoint(block, seg);
        if (driven && !driven->angleFormula.isEmpty()) {
            m_editAngle->setText(driven->angleFormula);
            m_lblFxAngle->setVisible(true);
        } else if (auto d = worldOrNominalDegOfSegment(block, seg)) {
            m_editAngle->setText(cad::geo::Units::formatDegValue(*d));
            m_lblFxAngle->setVisible(false);
        }
        m_editAngle->setPlaceholderText(cad::ui::kPlaceholderAngleOrFormula);
        return;
    }

    if (att) {
        const auto prof = modeProfile(att->rotationMode);
        m_editAngle->setPlaceholderText(prof.placeholder);
        const double radius = block ? block->segmentLengthAtPoint(att->fromPointId) : 0.0;
        const auto mv = getAttachmentModeValue(*att);
        const bool hasFormula = !mv.formula.isEmpty();
        m_editAngle->setText(hasFormula ? mv.formula
                                        : formatAttachmentDisplayValue(att->rotationMode, mv.value, radius));
        m_lblFxAngle->setVisible(hasFormula);
    }
}

void SegmentAngleCard::applyAngle()
{
    if (!m_doc) return;
    auto* block = m_doc->findBlock(m_blockId);
    if (!block) return;
    auto* seg = block->findSegment(m_segmentId);
    if (!seg) return;

    const auto parsed = cad::geo::parseAngleText(m_editAngle->text());
    if (parsed.formula.isEmpty()) return;

    double targetDeg = parsed.value;
    if (!parsed.isNumber) {
        auto r = cad::param::ConditionEngine::evaluate(
            parsed.formula, m_doc->parameters(), m_doc->conditions());
        if (!r.ok) return;
        targetDeg = r.value;
    }

    bool isFollower = false;
    bool isIndependentAngle = false;
    QUuid attId;
    for (const auto& att : m_doc->attachments()) {
        if (att.fromBlockId == m_blockId) {
            isFollower = true;
            isIndependentAngle = att.angleIndependent;
            attId = att.id;
            break;
        }
    }

    if (isFollower && !isIndependentAngle) {
        if (auto* att = m_doc->findAttachment(attId)) {
            // 目标三模态: 当前模字段按输入换算, 其余两模字段保持原值
            // (命令 verbatim 全字段写, 非当前模写入 = 原值重放)。
            double newAngle = att->followerAngle;
            QString newAngleFormula = att->followerAngleFormula;
            double newArc = att->arcLength;
            QString newArcFormula = att->arcLengthFormula;
            double newChord = att->chordLength;
            QString newChordFormula = att->chordLengthFormula;
            if (att->rotationMode == cad::param::RotationMode::ArcLength) {
                const double radius = block->segmentLengthAtPoint(att->fromPointId);
                const double foldDeg = (radius > cad::geo::kGeomEps)
                    ? cad::geo::arcMmToDeg(cad::geo::Units::cmToMm(targetDeg), radius) : 0.0;
                const double alphaDeg = cad::geo::normalizeDeg360(foldDeg);
                newArc = cad::geo::degToArcMm(alphaDeg, radius);
                newArcFormula = parsed.isNumber ? QString() : parsed.formula;
            } else if (att->rotationMode == cad::param::RotationMode::ChordLength) {
                const double radius = block->segmentLengthAtPoint(att->fromPointId);
                double chordMm = cad::geo::Units::cmToMm(targetDeg);
                if (radius > cad::geo::kGeomEps) {
                    chordMm = std::clamp(chordMm, -2.0 * radius, 2.0 * radius);
                }
                newChord = chordMm;
                newChordFormula = parsed.isNumber ? QString() : parsed.formula;
            } else {
                newAngle = cad::param::followerAngleToStorage(targetDeg);
                newAngleFormula = parsed.isNumber ? QString() : parsed.formula;
            }
            if (auto* stack = m_doc->undoStack()) {
                // 角度提交入栈 (数值连续编辑经 mergeWith 合并为一步)。
                stack->push(new cad::cmd::SetFollowerAngleCommand(
                    m_doc, attId, newAngle, newAngleFormula, att->rotationMode,
                    newArc, newArcFormula, newChord, newChordFormula));
            } else {
                att->followerAngle = newAngle;
                att->followerAngleFormula = newAngleFormula;
                att->arcLength = newArc;
                att->arcLengthFormula = newArcFormula;
                att->chordLength = newChord;
                att->chordLengthFormula = newChordFormula;
            }
        }
    } else {
        auto* ep = block->findPoint(seg->endPointId);
        if (!ep) return;
        // 目标态 = 当前模型态 + 角度覆盖 (与旧直写逐位一致; 非角度字段
        // 原样重放 = 命令无副作用)。自由→Polar 转换一并入栈。
        cad::cmd::SegmentEditBarCommand::State st =
            cad::cmd::SegmentEditBarCommand::State::captureFrom(*m_doc, m_blockId, m_segmentId);
        if (ep->constraint != cad::param::PointConstraint::OrthoOffset &&
            ep->constraint != cad::param::PointConstraint::Polar) {
            const auto* sp = block->findPoint(seg->startPointId);
            if (!sp || !sp->resolved || !ep->resolved) return;
            st.endConstraint = static_cast<int>(cad::param::PointConstraint::Polar);
            st.endRefPointId = seg->startPointId;
            st.endDistance = sp->resolvedPos.distanceTo(ep->resolvedPos);
        }
        const double rotDeg = cad::geo::radToDeg(block->transform.rotation);
        // 本地极角保持带符号（等价角，resolve 周期性无差异）；2026-09 D3 的
        // [0,360) 域只约束「世界方向」显示，读侧 normalizeDeg360 已覆盖。
        st.endAngle = targetDeg - rotDeg;
        st.endAngleFormula = (!parsed.isNumber)
            ? ((std::abs(rotDeg) > cad::geo::kGeomEps)
                ? QStringLiteral("(%1)-%2").arg(parsed.formula).arg(rotDeg, 0, 'g', 12)
                : parsed.formula)
            : QString();
        if (auto* stack = m_doc->undoStack()) {
            stack->push(new cad::cmd::SegmentEditBarCommand(
                m_doc, m_blockId, m_segmentId, st));
        } else {
            ep->constraint = static_cast<cad::param::PointConstraint>(st.endConstraint);
            ep->refPointId = st.endRefPointId;
            ep->distance = st.endDistance;
            ep->angle = st.endAngle;
            ep->angleFormula = st.endAngleFormula;
        }
    }

    m_editAngle->setStyleSheet(QString());
    m_lblFxAngle->setVisible(!parsed.isNumber && !parsed.formula.isEmpty());
    // 不在此处重新 populateAngleField(): 写入后几何尚未重解 (resolveAll 由
    // 对话框 refreshScene 延后触发), 此时按 resolvedPos 读世界角会拿到**旧值**
    // 并覆盖用户刚输入的内容 → "输入角度不断跳动" (用户报告 2026-12)。保留
    // 用户输入, 世界角读数由 onDocResolved (resolved 信号) 在重解后刷新。
    refresh();
    emit changed();
}

void SegmentAngleCard::onAngleDirty()
{
    m_editAngle->setStyleSheet(QString());
    const QString text = m_editAngle->text().trimmed();
    const bool isNumber = cad::geo::parseAngleText(text).isNumber;
    m_lblFxAngle->setVisible(!isNumber && !text.isEmpty());
    emit angleEdited();
}

void SegmentAngleCard::onModeToggle()
{
    const auto* att = findFollowerAttachment();
    if (!att || !m_doc) return;

    // 未应用的输入先落盘 (数值或公式都经 applyAngle; 公式随后随模式一起
    // 跨域换算 —— 用户刚输入未回车的公式不能被静默丢弃)。
    if (!m_editAngle->text().trimmed().isEmpty())
        applyAngle();

    auto* mutAtt = m_doc->findAttachment(att->id);
    if (!mutAtt) return;

    auto* blk = m_doc->findBlock(m_blockId);
    double radius = blk ? blk->segmentLengthAtPoint(mutAtt->fromPointId) : 0.0;
    const auto target = (mutAtt->rotationMode == cad::param::RotationMode::Angle)
        ? cad::param::RotationMode::ArcLength
        : ((mutAtt->rotationMode == cad::param::RotationMode::ArcLength)
            ? cad::param::RotationMode::ChordLength : cad::param::RotationMode::Angle);
    // 2026-12: 公式驱动不再拒绝切换 —— 公式跨域换算保留变量链接 (半径烘焙
    // 为常数), 见 FollowerAngle.h。数值路径保持历史 fmod 语义。
    const auto res = cad::param::followerModeSwitchValues(
        *mutAtt, radius, target, m_doc->parameters(), m_doc->conditions());
    const double newAngle = (target == cad::param::RotationMode::Angle) ? res.angle : mutAtt->followerAngle;
    const QString newAngleFormula = (target == cad::param::RotationMode::Angle) ? res.angleFormula : mutAtt->followerAngleFormula;
    const double newArc = (target == cad::param::RotationMode::ArcLength) ? res.arcMm : mutAtt->arcLength;
    const QString newArcFormula = (target == cad::param::RotationMode::ArcLength) ? res.arcFormula : mutAtt->arcLengthFormula;
    const double newChord = (target == cad::param::RotationMode::ChordLength) ? res.chordMm : mutAtt->chordLength;
    const QString newChordFormula = (target == cad::param::RotationMode::ChordLength) ? res.chordFormula : mutAtt->chordLengthFormula;

    if (auto* stack = m_doc->undoStack()) {
        stack->push(new cad::cmd::SetFollowerAngleCommand(
            m_doc, mutAtt->id, newAngle, newAngleFormula, target,
            newArc, newArcFormula, newChord, newChordFormula));
    } else {
        mutAtt->rotationMode = target;
        mutAtt->followerAngle = newAngle;
        mutAtt->followerAngleFormula = newAngleFormula;
        mutAtt->arcLength = newArc;
        mutAtt->arcLengthFormula = newArcFormula;
        mutAtt->chordLength = newChord;
        mutAtt->chordLengthFormula = newChordFormula;
    }
    m_doc->resolveAll();
    populateAngleField();
    refresh();
    emit changed();
}

void SegmentAngleCard::updateWorldAngleLabel(const cad::param::Attachment& att)
{
    if (!m_doc) { m_lblWorldAngle->setVisible(false); return; }
    // 有效基准方向 = 与 Resolver 同构 (自定义角度基准/两点连线全部生效,
    // 2026-09 审核 F1) —— 此前只取位置宿主出方向, 设了自定义基准后读数与
    // 线实际方向不符。
    const double refWorldDeg = cad::geo::radToDeg(cad::param::effectiveAngleRefWorld(m_doc, att));
    const auto* block = m_doc->blocksView().byId(m_blockId);
    const double radius = block ? block->segmentLengthAtPoint(att.fromPointId) : 0.0;
    const double constDeg = attachmentEffectiveAngleDeg(att, m_doc, radius);
    const double absDeg = cad::geo::normalizeDeg360(refWorldDeg + 180.0 - constDeg);
    const QString text = QString::fromUtf8("= 世界角度 %1°")
                             .arg(cad::geo::Units::formatDegValue(absDeg));
    if (m_lblWorldAngle->text() != text)
        m_lblWorldAngle->setText(text);
    m_lblWorldAngle->setVisible(true);
}

void SegmentAngleCard::onDocResolved()
{
    // 外部几何变更 (旋转拖动等) 每帧广播 resolved: 输入框未聚焦时回填
    // 数值/公式 (与 ContextStrip 同款焦点保护 —— 聚焦中回填会打断敲击,
    // 且用户未提交的输入不能被覆盖)。桥接线除外: 其输入框内容由
    // setBridgeReadOnly 维护 (测出的世界角), 回填会覆盖成存储的跟随角。
    if (!m_editAngle->hasFocus()) {
        const auto* blk = m_doc ? m_doc->blocksView().byId(m_blockId) : nullptr;
        if (!(blk && blk->isBridge)) {
            const QSignalBlocker sb(m_editAngle);
            populateAngleField();
            refresh();
            return;
        }
    }
    // 聚焦中: 只刷几何相关读数 (绝对角度/世界角度), 不覆盖输入。
    const auto* att = findFollowerAttachment();
    if (att && !att->angleIndependent) {
        updateWorldAngleLabel(*att);
        return;
    }
    // 自由线/独立角: 世界角度是纯几何读数, 每次重解后重算刷新
    // (换向等模型变更经 resolveAll 广播, 漏刷会让标签消失/滞留旧值)。
    const auto* block = m_doc ? m_doc->blocksView().byId(m_blockId) : nullptr;
    const auto* seg = block ? block->findSegment(m_segmentId) : nullptr;
    if (auto d = worldOrNominalDegOfSegment(block, seg)) {
        const QString text = QString::fromUtf8("= 世界角度 %1°")
            .arg(cad::geo::Units::formatDegValue(*d));
        if (m_lblWorldAngle->text() != text)
            m_lblWorldAngle->setText(text);
        m_lblWorldAngle->setVisible(true);
        return;
    }
    m_lblWorldAngle->setVisible(false);
}

void SegmentAngleCard::setBridgeReadOnly(bool bridge)
{
    if (!bridge || !m_doc) return;
    const auto* block = m_doc->blocksView().byId(m_blockId);
    if (!block || !block->isBridge) return;
    const auto* seg = block->findSegment(m_segmentId);
    if (!seg) return;

    if (auto d = worldDegOfSegment(block, seg)) {
        m_editAngle->setText(cad::geo::Units::formatDegValue(*d));
    }
    m_editAngle->setEnabled(false);
    m_lblFxAngle->setVisible(false);
    m_lblWorldAngle->setVisible(false);
    m_lblFollowValue->setVisible(false);
    m_lblCaption->setText(QString::fromUtf8("角度"));
    m_lblCaption->setStyleSheet(QString());
    const QString tip = cad::ui::TooltipFormatter::status(
        QStringLiteral("桥接线（只读）"),
        QStringLiteral("长度与角度完全由两端吸附钉住的宿主几何决定，不可直接编辑"),
        false);
    m_editAngle->setToolTip(tip);
}

} // namespace cad::ui
