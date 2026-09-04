#include "ui/LineEndpointSection.h"

#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QFrame>
#include <QPushButton>
#include <QSignalBlocker>

#include "ElaLineEdit.h"
#include "ElaText.h"

#include "parametric/ParamDocument.h"
#include "parametric/Block.h"
#include "parametric/Serial.h"
#include "parametric/AttachmentGraph.h"
#include "canvas/CanvasScene.h"
#include "geometry/Units.h"
#include "geometry/CurveMath.h"
#include "geometry/Angle.h"
#include "parametric/FollowerAngle.h"
#include "ui/Theme.h"
#include "ui/FormScaffold.h"
#include "ui/TooltipFormatter.h"
#include "ui/NoteButton.h"
#include "ui/PointRefEdit.h"
#include "document/commands/AttachmentCommands.h"
#include "document/commands/ReverseSegmentCommand.h"
#include "document/commands/SegmentPropertyCommands.h"

namespace cad::ui {

namespace {

constexpr int kLabelW = 64;
constexpr int kFieldH = 26;

const cad::param::Attachment* findFollowerAttachment(const cad::param::ParamDocument* doc,
                                                    const QUuid& blockId)
{
    if (!doc) return nullptr;
    for (const auto& att : doc->attachments()) {
        if (!att.isPin && att.fromBlockId == blockId)
            return &att;
    }
    return nullptr;
}

} // namespace

QUuid LineEndpointSection::fixedTopPointId(const cad::param::Block* block,
                                          const cad::param::Segment* seg)
{
    if (!block || !seg) return QUuid();
    int idxStart = -1, idxEnd = -1, i = 0;
    for (const auto& pt : block->points) {
        if (pt.id == seg->startPointId) idxStart = i;
        if (pt.id == seg->endPointId)   idxEnd = i;
        ++i;
    }
    if (idxStart < 0 || idxEnd < 0) return QUuid();
    return idxStart <= idxEnd ? seg->startPointId : seg->endPointId;
}

LineEndpointSection::LineEndpointSection(cad::param::ParamDocument* paramDoc,
                                         CanvasScene* scene,
                                         QWidget* parent)
    : QWidget(parent)
    , m_paramDoc(paramDoc)
    , m_scene(scene)
{
    const QString chips = cad::ui::chipButtonStyle();

    auto* col = new QVBoxLayout(this);
    col->setContentsMargins(0, 0, 0, 0);
    col->setSpacing(4);

    auto* startPanel = buildEndpoint(/*isStart=*/true);
    col->addWidget(startPanel);

    // ── 朝向轴 (§6.1): 垂直虚线 + 换向箭头嵌在中段 ──
    m_btnDirectionArrow = new QPushButton(QString::fromUtf8("↓"), this);
    m_btnDirectionArrow->setObjectName(QStringLiteral("directionArrowBtn"));
    m_btnDirectionArrow->setFixedSize(28, kFieldH);
    m_btnDirectionArrow->setStyleSheet(chips);
    m_btnDirectionArrow->setCursor(Qt::PointingHandCursor);
    m_btnDirectionArrow->setToolTip(cad::ui::TooltipFormatter::action(
        QStringLiteral("调换进/出"),
        QStringLiteral("↓ = 上进下出，↑ = 下进上出（点位置固定，几何不变）")));
    connect(m_btnDirectionArrow, &QPushButton::clicked,
            this, &LineEndpointSection::onDirectionArrowClickedInternal);

    {
        auto* axis = new QWidget(this);
        axis->setFixedHeight(kFieldH + 12);
        auto* al = new QHBoxLayout(axis);
        al->setContentsMargins(kLabelW + 6, 0, 0, 0);
        al->setSpacing(8);
        auto* line = new QFrame(axis);
        line->setObjectName(QStringLiteral("endpointAxis"));
        line->setFixedSize(1, kFieldH + 10);
        al->addWidget(line);
        al->addWidget(m_btnDirectionArrow);
        al->addStretch();
        col->addWidget(axis);
    }

    auto* endPanel = buildEndpoint(/*isStart=*/false);
    col->addWidget(endPanel);
}

QWidget* LineEndpointSection::buildEndpoint(bool isStart)
{
    const QString chips = cad::ui::chipButtonStyle();
    const QString dimMono = cad::ui::Theme::dimValueStyle();

    ElaText*& ptBadge = isStart ? m_lblStartPtId : m_lblEndPtId;
    ElaLineEdit*& nameEdit = isStart ? m_editStartName : m_editEndName;
    NoteButton*& noteBtn = isStart ? m_noteStart : m_noteEnd;
    QPushButton*& showChip = isStart ? m_chkShowStartName : m_chkShowEndName;
    ElaLineEdit*& extEdit = isStart ? m_editStartExtend : m_editEndExtend;
    PointRefEdit*& refConn = isStart ? m_refStartConnect : m_refEndConnect;
    QPushButton*& detachBtn = isStart ? m_btnStartDetach : m_btnEndDetach;
    ElaText*& connSummary = isStart ? m_lblStartConn : m_lblEndConn;

    auto* panel = new QWidget(this);
    panel->setObjectName(isStart ? QStringLiteral("startPointCard")
                                 : QStringLiteral("endPointCard"));
    auto* v = new QVBoxLayout(panel);
    v->setContentsMargins(kLabelW, 0, 0, 0);
    v->setSpacing(4);

    // ── 首行: [P1徽章] [名称输入(150)][便利贴][显示] ──
    {
        auto* head = new QHBoxLayout();
        head->setSpacing(6);
        ptBadge = new ElaText(QString(), 11, panel);
        ptBadge->setObjectName(QStringLiteral("endpointBadge"));
        ptBadge->setStyleSheet(QStringLiteral(
            "#endpointBadge { font-weight:600; %1 }")
            .arg(QString::fromLatin1(cad::ui::ThemeTokens::kMonospaceFamily)));
        ptBadge->setFixedWidth(44);
        ptBadge->setAlignment(Qt::AlignCenter);
        ptBadge->setToolTip(cad::ui::TooltipFormatter::status(
            isStart ? QStringLiteral("点1 (起点)") : QStringLiteral("点2 (终点)"),
            isStart ? QStringLiteral("上组 = 点1（线段第一个点，位置固定）；进出方向见中间箭头")
                    : QStringLiteral("下组 = 点2（线段第二个点，位置固定）；进出方向见中间箭头"),
            false));
        head->addWidget(ptBadge);

        nameEdit = new ElaLineEdit(panel);
        nameEdit->setFixedWidth(150);
        nameEdit->setFixedHeight(kFieldH);
        nameEdit->setStyleSheet(QStringLiteral("font-size: 11px;"));
        nameEdit->setPlaceholderText(QString::fromUtf8("名称，如“肩点”"));
        connect(nameEdit, &ElaLineEdit::textChanged, this, &LineEndpointSection::liveUpdated);
        head->addWidget(nameEdit);

        noteBtn = new NoteButton(panel);
        noteBtn->setPlaceholder(QString::fromUtf8("这个点的说明…"));
        connect(noteBtn, &NoteButton::noteEdited, this,
                isStart ? &LineEndpointSection::onStartNoteEdited
                        : &LineEndpointSection::onEndNoteEdited);
        head->addWidget(noteBtn);

        showChip = new QPushButton(QString::fromUtf8("显示"), panel);
        showChip->setCheckable(true);
        showChip->setFixedHeight(kFieldH);
        showChip->setStyleSheet(chips);
        showChip->setCursor(Qt::PointingHandCursor);
        showChip->setToolTip(cad::ui::TooltipFormatter::action(
            QStringLiteral("显示端点名称"),
            QStringLiteral("在画布上显示该点名称")));
        connect(showChip, &QPushButton::toggled, this, &LineEndpointSection::liveUpdated);
        head->addWidget(showChip);
        head->addStretch();
        v->addLayout(head);
    }

    // ── 值行组: 延长量 + 连接到 ──
    {
        auto* r = new QHBoxLayout();
        r->setSpacing(6);
        auto* lblExt = new ElaText(QString::fromUtf8("延长量"), 11, panel);
        r->addWidget(lblExt);
        extEdit = new ElaLineEdit(panel);
        extEdit->setObjectName(isStart ? QStringLiteral("startExtendEdit")
                                       : QStringLiteral("endExtendEdit"));
        extEdit->setFixedWidth(150);
        extEdit->setFixedHeight(kFieldH);
        extEdit->setStyleSheet(QStringLiteral("font-size: 11px;"));
        extEdit->setPlaceholderText(QString::fromUtf8("0"));
        extEdit->setToolTip(cad::ui::TooltipFormatter::action(
            QStringLiteral("延长量 (cm)"),
            QStringLiteral("沿该端朝外方向延长的距离；数值或公式 cm；只允许 >= 0")));
        connect(extEdit, &ElaLineEdit::editingFinished, this,
                isStart ? &LineEndpointSection::onStartExtendEdited
                        : &LineEndpointSection::onEndExtendEdited);
        r->addWidget(extEdit);
        r->addStretch();
        v->addLayout(r);

        auto* rc = new QHBoxLayout();
        rc->setSpacing(6);
        auto* lblConn = new ElaText(QString::fromUtf8("连接到"), 11, panel);
        rc->addWidget(lblConn);
        refConn = new PointRefEdit(m_paramDoc, panel);
        refConn->setObjectName(isStart ? QStringLiteral("startConnectEdit")
                                       : QStringLiteral("endConnectEdit"));
        refConn->setFixedWidth(150);
        refConn->setFixedHeight(kFieldH);
        refConn->setToolTip(cad::ui::TooltipFormatter::action(
            isStart ? QStringLiteral("起点连接") : QStringLiteral("终点指向"),
            isStart ? QStringLiteral("输入目标点 P# 或线段 L#/名称，回车建立/重定向跟随连接（吸附；本端为进/起点时可用）")
                    : QStringLiteral("输入目标点 P# 或线段 L#/名称，回车建立/重定向终点指向（本端为出/终点时可用）")));
        connect(refConn, &PointRefEdit::pointResolved, this,
                [this, isStart](const QUuid& bId, const QUuid& pId) {
                    if (isStart) onStartConnectResolved(bId, pId);
                    else onEndConnectResolved(bId, pId);
                });
        rc->addWidget(refConn);

        detachBtn = new QPushButton(QString::fromUtf8("拆开"), panel);
        detachBtn->setObjectName(isStart ? QStringLiteral("startDetachBtn")
                                         : QStringLiteral("endDetachBtn"));
        detachBtn->setFixedSize(48, kFieldH);
        detachBtn->setStyleSheet(chips);
        detachBtn->setCursor(Qt::PointingHandCursor);
        connect(detachBtn, &QPushButton::clicked, this,
                isStart ? &LineEndpointSection::onStartDetachClicked
                        : &LineEndpointSection::onEndDetachClicked);
        rc->addWidget(detachBtn);
        rc->addStretch();
        v->addLayout(rc);
    }

    connSummary = new ElaText(QString(), 11, panel);
    connSummary->setObjectName(isStart ? QStringLiteral("startPointConn")
                                       : QStringLiteral("endPointConn"));
    connSummary->setStyleSheet(dimMono);
    connSummary->setToolTip(cad::ui::TooltipFormatter::status(
        QStringLiteral("端点连接关系"),
        QStringLiteral("跟随 = 本线跟随的基准线；挂载 = 吸附在本端点上的下游线（反向连接）；指向 = 本线终点指向的目标线"),
        false));
    connSummary->setVisible(false);
    v->addWidget(connSummary);

    return panel;
}

void LineEndpointSection::setTarget(const QUuid& blockId, const QUuid& segmentId)
{
    m_blockId = blockId;
    m_segmentId = segmentId;
    if (m_refStartConnect) m_refStartConnect->setExcludeBlock(blockId);
    if (m_refEndConnect)   m_refEndConnect->setExcludeBlock(blockId);
}

void LineEndpointSection::populateFromModel(const cad::param::Block& block,
                                            const cad::param::Segment& seg)
{
    const QUuid topId = fixedTopPointId(&block, &seg);
    const QUuid botId = topId.isNull()
        ? QUuid()
        : (topId == seg.startPointId ? seg.endPointId : seg.startPointId);
    m_topIsStart = (topId == seg.startPointId);

    const auto* ptTop = block.findPoint(topId);
    const auto* ptBot = block.findPoint(botId);

    if (m_lblStartPtId)
        m_lblStartPtId->setText(ptTop ? cad::param::Serial::tag(ptTop->serial) : QString());
    if (m_lblEndPtId)
        m_lblEndPtId->setText(ptBot ? cad::param::Serial::tag(ptBot->serial) : QString());

    if (m_editStartName && ptTop) {
        const QSignalBlocker b(m_editStartName);
        m_editStartName->setText(ptTop->name);
    }
    if (m_chkShowStartName && ptTop) {
        const QSignalBlocker b(m_chkShowStartName);
        m_chkShowStartName->setChecked(ptTop->showName);
    }
    if (m_noteStart && ptTop) {
        m_noteStart->setText(ptTop->annotation);
    }

    if (m_editEndName && ptBot) {
        const QSignalBlocker b(m_editEndName);
        m_editEndName->setText(ptBot->name);
    }
    if (m_chkShowEndName && ptBot) {
        const QSignalBlocker b(m_chkShowEndName);
        m_chkShowEndName->setChecked(ptBot->showName);
    }
    if (m_noteEnd && ptBot) {
        m_noteEnd->setText(ptBot->annotation);
    }

    refreshEndpointConnRows();
    refreshEndpointExtends();
    refreshDirectionArrow();
}

void LineEndpointSection::applyToModel(cad::param::Block* block, cad::param::Segment* seg)
{
    if (!block || !seg) return;
    const QUuid topPtId = fixedTopPointId(block, seg);
    const QUuid botPtId = topPtId.isNull()
        ? QUuid()
        : (topPtId == seg->startPointId ? seg->endPointId : seg->startPointId);

    if (auto* p = block->findPoint(topPtId)) {
        if (m_editStartName) p->name = m_editStartName->text();
        if (m_chkShowStartName) p->showName = m_chkShowStartName->isChecked();
        if (m_noteStart) p->annotation = m_noteStart->text();
    }
    if (auto* p = block->findPoint(botPtId)) {
        if (m_editEndName) p->name = m_editEndName->text();
        if (m_chkShowEndName) p->showName = m_chkShowEndName->isChecked();
        if (m_noteEnd) p->annotation = m_noteEnd->text();
    }
}

QString LineEndpointSection::startName() const
{
    return m_editStartName ? m_editStartName->text() : QString();
}

QString LineEndpointSection::endName() const
{
    return m_editEndName ? m_editEndName->text() : QString();
}

QString LineEndpointSection::startAnnotation() const
{
    return m_noteStart ? m_noteStart->text() : QString();
}

QString LineEndpointSection::endAnnotation() const
{
    return m_noteEnd ? m_noteEnd->text() : QString();
}

bool LineEndpointSection::startShowName() const
{
    return m_chkShowStartName ? m_chkShowStartName->isChecked() : false;
}

bool LineEndpointSection::endShowName() const
{
    return m_chkShowEndName ? m_chkShowEndName->isChecked() : false;
}

void LineEndpointSection::refreshEndpointConnRows()
{
    if (!m_lblStartConn || !m_lblEndConn) return;
    if (!m_paramDoc) return;
    const auto* block = m_paramDoc->findBlock(m_blockId);
    const auto* seg = block ? block->findSegment(m_segmentId) : nullptr;
    if (!block || !seg) {
        m_lblStartConn->setText(QString());
        m_lblEndConn->setText(QString());
        m_lblStartConn->setVisible(false);
        m_lblEndConn->setVisible(false);
        return;
    }

    auto endpointText = [&](const QUuid& pointId, bool isEnd) -> QString {
        QStringList parts;
        for (const auto& att : m_paramDoc->attachments()) {
            if (att.isPin) continue;
            if (att.fromBlockId == m_blockId && att.fromPointId == pointId) {
                if (const auto* ldr = m_paramDoc->findBlock(att.toBlockId)) {
                    if (ldr->isShadow) {
                        const auto* master = m_paramDoc->findBlock(ldr->shadowMasterBlockId);
                        const cad::param::Attachment* att1 = nullptr;
                        for (const auto& a : m_paramDoc->attachments()) {
                            if (!a.isPin && a.fromBlockId == ldr->id) {
                                att1 = &a; break;
                            }
                        }
                        auto hostLabel = [&](const cad::param::Block* hb, const QUuid& sId) {
                            if (!hb) return QString();
                            const auto* hs = hb->findSegment(sId);
                            if (!hs) return QString();
                            QString t = cad::param::Serial::tag(hs->serial);
                            if (!hs->name.isEmpty()) t += QStringLiteral("·") + hs->name;
                            return t;
                        };
                        if (att1) {
                            const QString t = hostLabel(m_paramDoc->findBlock(att1->toBlockId), att1->toSegmentId);
                            if (!t.isEmpty())
                                parts << QString::fromUtf8("跟随 ") + t + QString::fromUtf8("（经影子）");
                        } else if (master && !master->segments.empty()) {
                            const QString t = hostLabel(master, master->segments.front().id);
                            if (!t.isEmpty())
                                parts << QString::fromUtf8("角度基准 ") + t + QString::fromUtf8("（影子）");
                        }
                    } else if (const auto* ls = ldr->findSegment(att.toSegmentId)) {
                        QString t = cad::param::Serial::tag(ls->serial);
                        if (!ls->name.isEmpty()) t += QStringLiteral("·") + ls->name;
                        parts << QString::fromUtf8("跟随 ") + t;
                    }
                }
                break;
            }
        }
        for (const auto& att : m_paramDoc->attachments()) {
            if (att.isPin) continue;
            if (att.toBlockId != m_blockId || att.toPointId != pointId) continue;
            if (const auto* fb = m_paramDoc->findBlock(att.fromBlockId)) {
                const QUuid fs = fb->exitSegmentAtPoint(att.fromPointId);
                if (const auto* fsg = fb->findSegment(fs)) {
                    QString t = cad::param::Serial::tag(fsg->serial);
                    if (!fsg->name.isEmpty()) t += QStringLiteral("·") + fsg->name;
                    parts << QString::fromUtf8("挂载 ") + t;
                }
            }
        }
        if (isEnd && !block->endTargetPointId.isNull()) {
            if (const auto* tb = m_paramDoc->findBlock(block->endTargetBlockId)) {
                const QUuid ts = tb->exitSegmentAtPoint(block->endTargetPointId);
                if (const auto* tsg = tb->findSegment(ts)) {
                    QString t = cad::param::Serial::tag(tsg->serial);
                    if (!tsg->name.isEmpty()) t += QStringLiteral("·") + tsg->name;
                    parts << QString::fromUtf8("指向 ") + t;
                }
            }
        }
        return parts.join(QStringLiteral("  "));
    };

    const QUuid topId = fixedTopPointId(block, seg);
    const QUuid botId = topId.isNull() ? QUuid() : (topId == seg->startPointId ? seg->endPointId : seg->startPointId);
    const bool topIsEnd = (topId == seg->endPointId);

    const QString startT = endpointText(topId, topIsEnd);
    const QString endT   = endpointText(botId, !topIsEnd);
    m_lblStartConn->setText(startT);
    m_lblEndConn->setText(endT);
    m_lblStartConn->setVisible(!startT.isEmpty());
    m_lblEndConn->setVisible(!endT.isEmpty());

    // Follower attachment on top slot
    if (m_refStartConnect) {
        const auto* att = findFollowerAttachment(m_paramDoc, m_blockId);
        const QSignalBlocker sb1(m_refStartConnect);
        if (att && m_topIsStart && !att->angleOnly) {
            const auto* toBlk = m_paramDoc->findBlock(att->toBlockId);
            if (toBlk && toBlk->isShadow) {
                const cad::param::Attachment* att1 = nullptr;
                for (const auto& a : m_paramDoc->attachments()) {
                    if (!a.isPin && a.fromBlockId == toBlk->id) {
                        att1 = &a; break;
                    }
                }
                if (att1)
                    m_refStartConnect->setPoint(att1->toBlockId, att1->toPointId);
                else
                    m_refStartConnect->clearPoint();
            } else {
                m_refStartConnect->setPoint(att->toBlockId, att->toPointId);
            }
            if (m_btnStartDetach) {
                m_btnStartDetach->setEnabled(true);
                m_btnStartDetach->setText(QString::fromUtf8("拆开"));
            }
        } else if (att && m_topIsStart) {
            m_refStartConnect->clearPoint();
            if (m_btnStartDetach) {
                m_btnStartDetach->setEnabled(true);
                m_btnStartDetach->setText(QString::fromUtf8("重连"));
            }
        } else {
            m_refStartConnect->clearPoint();
            if (m_btnStartDetach) {
                m_btnStartDetach->setEnabled(false);
                m_btnStartDetach->setText(QString::fromUtf8("拆开"));
            }
        }
    }

    // End target on bottom slot
    if (m_refEndConnect) {
        const QSignalBlocker sb2(m_refEndConnect);
        if (!block->endTargetPointId.isNull() && !m_topIsStart) {
            m_refEndConnect->setPoint(block->endTargetBlockId,
                                      block->endTargetPointId);
            if (m_btnEndDetach) {
                m_btnEndDetach->setEnabled(true);
                m_btnEndDetach->setText(QString::fromUtf8("拆开"));
            }
        } else {
            m_refEndConnect->clearPoint();
            if (m_btnEndDetach) {
                m_btnEndDetach->setEnabled(false);
                m_btnEndDetach->setText(QString::fromUtf8("拆开"));
            }
        }
    }
}

void LineEndpointSection::refreshEndpointExtends()
{
    if (!m_editStartExtend || !m_editEndExtend) return;
    if (!m_paramDoc) return;
    auto* block = m_paramDoc->findBlock(m_blockId);
    auto* seg = block ? block->findSegment(m_segmentId) : nullptr;
    if (!block || !seg) return;

    QString cardReason;
    if (seg->isCurve())
        cardReason = QString::fromUtf8("曲线暂不支持延长");
    else if (block->isBridge)
        cardReason = QString::fromUtf8("桥接线两端均已钉住，不支持延长");
    else if (block->isDart())
        cardReason = QString::fromUtf8("省道线为计算线，不支持延长");

    const bool wholeGray = !cardReason.isEmpty();
    const QUuid topId = fixedTopPointId(block, seg);
    const QUuid botId = topId.isNull()
        ? QUuid()
        : (topId == seg->startPointId ? seg->endPointId : seg->startPointId);
    const bool topIsStart = (topId == seg->startPointId);
    const QString topReason = wholeGray ? cardReason
        : endpointExtendDisableReason(*block, *seg, topId);
    const QString botReason = wholeGray ? cardReason
        : endpointExtendDisableReason(*block, *seg, botId);

    const QSignalBlocker b1(m_editStartExtend);
    const QSignalBlocker b2(m_editEndExtend);
    const auto extendText = [seg](bool roleIsStart) {
        const QString& f = roleIsStart ? seg->extendStartFormula
                                       : seg->extendEndFormula;
        const double mm = roleIsStart ? seg->extendStartMm : seg->extendEndMm;
        return !f.isEmpty() ? f
             : (mm > 0.0 ? cad::geo::Units::formatCmTrimmed(mm) : QString());
    };
    m_editStartExtend->setText(extendText(topIsStart));
    m_editEndExtend->setText(extendText(!topIsStart));
    m_editStartExtend->setEnabled(topReason.isEmpty());
    m_editEndExtend->setEnabled(botReason.isEmpty());
}

QString LineEndpointSection::endpointExtendDisableReason(const cad::param::Block& block,
                                                         const cad::param::Segment& seg,
                                                         const QUuid& pointId) const
{
    if (pointId.isNull()) return QString::fromUtf8("端点缺失");
    for (const auto& att : m_paramDoc->attachments()) {
        if (att.fromBlockId == block.id && !att.isPin && att.fromPointId == pointId)
            return QString::fromUtf8("该端已粘在基准线上（跟随连接端），不允许延长");
    }
    int incidence = 0;
    for (const auto& s : block.segments) {
        if (s.startPointId == pointId || s.endPointId == pointId)
            ++incidence;
    }
    if (incidence > 1)
        return QString::fromUtf8("该端为折线/闭合轮廓的共用角点，暂不支持延长");

    const auto* comp = m_paramDoc->componentsView().ofBlock(block.id);
    if (comp && comp->exposedPointId == pointId)
        return QString::fromUtf8("该端为组件的暴露端点，暂不支持延长");

    return QString();
}

void LineEndpointSection::applyEndpointExtend(ElaLineEdit* edit, bool isTop)
{
    if (!m_paramDoc || !edit) return;
    auto* block = m_paramDoc->findBlock(m_blockId);
    auto* seg = block ? block->findSegment(m_segmentId) : nullptr;
    if (!block || !seg) return;

    const QUuid topId = fixedTopPointId(block, seg);
    const bool slotIsStart = isTop ? (topId == seg->startPointId) : (topId != seg->startPointId);

    const auto parsed = cad::geo::parseNumberOrFormula(edit->text());
    const bool isNumber = parsed.isNumber;
    const double num = parsed.value;

    cad::cmd::SetSegmentExtendCommand::Values v;
    v.startMm = seg->extendStartMm;
    v.startFormula = seg->extendStartFormula;
    v.endMm = seg->extendEndMm;
    v.endFormula = seg->extendEndFormula;
    if (slotIsStart) {
        if (parsed.formula.isEmpty()) { v.startMm = 0.0; v.startFormula.clear(); }
        else if (isNumber) { v.startMm = cad::geo::Units::cmToMm(num); v.startFormula.clear(); }
        else v.startFormula = parsed.formula;
    } else {
        if (parsed.formula.isEmpty()) { v.endMm = 0.0; v.endFormula.clear(); }
        else if (isNumber) { v.endMm = cad::geo::Units::cmToMm(num); v.endFormula.clear(); }
        else v.endFormula = parsed.formula;
    }

    m_paramDoc->undoStack()->push(new cad::cmd::SetSegmentExtendCommand(
        m_paramDoc, m_blockId, m_segmentId, v));
    refreshEndpointExtends();
    emit liveUpdated();
}

void LineEndpointSection::onStartExtendEdited()
{
    applyEndpointExtend(m_editStartExtend, true);
}

void LineEndpointSection::onEndExtendEdited()
{
    applyEndpointExtend(m_editEndExtend, false);
}

void LineEndpointSection::onStartNoteEdited(const QString& text)
{
    if (!m_paramDoc) return;
    if (auto* b = m_paramDoc->findBlock(m_blockId)) {
        if (auto* s = b->findSegment(m_segmentId)) {
            if (auto* p = b->findPoint(fixedTopPointId(b, s)))
                p->annotation = text;
        }
    }
}

void LineEndpointSection::onEndNoteEdited(const QString& text)
{
    if (!m_paramDoc) return;
    if (auto* b = m_paramDoc->findBlock(m_blockId)) {
        if (auto* s = b->findSegment(m_segmentId)) {
            const QUuid topId = fixedTopPointId(b, s);
            const QUuid botId = topId.isNull() ? QUuid() : (topId == s->startPointId ? s->endPointId : s->startPointId);
            if (auto* p = b->findPoint(botId))
                p->annotation = text;
        }
    }
}

void LineEndpointSection::onStartConnectResolved(const QUuid& blockId, const QUuid& pointId)
{
    if (!m_paramDoc) return;
    auto* block = m_paramDoc->findBlock(m_blockId);
    auto* seg = block ? block->findSegment(m_segmentId) : nullptr;
    if (!block || !seg) return;
    if (!m_topIsStart) { refreshEndpointConnRows(); return; }

    const auto* att = findFollowerAttachment(m_paramDoc, m_blockId);
    if (att) {
        auto* mut = m_paramDoc->findAttachment(att->id);
        if (!mut) return;
        const auto* leader = m_paramDoc->findBlock(blockId);
        if (!leader || !leader->findPoint(pointId)) { refreshEndpointConnRows(); return; }
        bool shadowRouted = false;
        if (const auto* curTo = m_paramDoc->findBlock(mut->toBlockId); curTo && curTo->isShadow) {
            if (blockId == curTo->shadowMasterBlockId)
                m_paramDoc->reattachShadowToMaster(mut->id, pointId);
            else
                m_paramDoc->mountShadowTo(curTo->id, blockId, pointId);
            shadowRouted = true;
        }
        if (!shadowRouted) {
            cad::param::preserveAngleRefOnReattach(m_paramDoc, *mut);
            mut->angleOnly = false;
            mut->isLocked = true;
            mut->slideMode = cad::param::SlideMode::None;
            mut->toBlockId = blockId;
            mut->toPointId = pointId;
            mut->toSegmentId = leader->exitSegmentAtPoint(pointId);
            if (auto* s = block->findSegment(m_segmentId)) {
                const double refWorld = cad::param::effectiveAngleRefWorld(m_paramDoc, *mut);
                const double localDir = block->directionAtPoint(s->startPointId);
                mut->followerAngle = cad::param::backSolveFollowerAngle(
                    block->transform.rotation, localDir, refWorld);
            }
            mut->followerAngleFormula.clear();
            mut->rotationMode = cad::param::RotationMode::Angle;
            mut->arcLength = 0.0;
            mut->arcLengthFormula.clear();
            m_paramDoc->resolveAll();
        }
    } else {
        const auto* leader = m_paramDoc->findBlock(blockId);
        if (!leader || !leader->findPoint(pointId)) { refreshEndpointConnRows(); return; }
        cad::param::Attachment attNew;
        attNew.fromBlockId = m_blockId;
        attNew.fromPointId = seg->startPointId;
        attNew.toBlockId = blockId;
        attNew.toPointId = pointId;
        attNew.toSegmentId = leader->exitSegmentAtPoint(pointId);
        const double refWorld = leader->transform.rotation + leader->exitDirectionAtPoint(pointId, attNew.toSegmentId);
        const double localDir = block->directionAtPoint(seg->startPointId);
        attNew.followerAngle = cad::param::backSolveFollowerAngle(block->transform.rotation, localDir, refWorld);
        m_paramDoc->addAttachment(attNew);
    }
    refreshEndpointConnRows();
    emit connectionChanged();
}

void LineEndpointSection::onStartDetachClicked()
{
    if (!m_paramDoc) return;
    const auto* att = findFollowerAttachment(m_paramDoc, m_blockId);
    if (!att || !m_topIsStart) { refreshEndpointConnRows(); return; }
    if (auto* stack = m_paramDoc->undoStack())
        stack->push(new cad::cmd::SetAttachmentAngleOnlyCommand(m_paramDoc, att->id, !att->angleOnly));
    else
        m_paramDoc->setAttachmentAngleOnly(att->id, !att->angleOnly);
    refreshEndpointConnRows();
    emit connectionChanged();
}

void LineEndpointSection::onEndConnectResolved(const QUuid& blockId, const QUuid& pointId)
{
    if (!m_paramDoc) return;
    auto* block = m_paramDoc->findBlock(m_blockId);
    if (!block || m_topIsStart) { refreshEndpointConnRows(); return; }
    const auto* targetBlock = m_paramDoc->findBlock(blockId);
    if (!targetBlock || !targetBlock->findPoint(pointId)) { refreshEndpointConnRows(); return; }
    if (blockId == m_blockId) { refreshEndpointConnRows(); return; }

    block->endTargetBlockId = blockId;
    block->endTargetPointId = pointId;
    m_paramDoc->resolveAll();
    refreshEndpointConnRows();
    emit connectionChanged();
}

void LineEndpointSection::onEndDetachClicked()
{
    if (!m_paramDoc) return;
    auto* block = m_paramDoc->findBlock(m_blockId);
    if (!block) return;
    block->endTargetBlockId = QUuid();
    block->endTargetPointId = QUuid();
    m_paramDoc->resolveAll();
    refreshEndpointConnRows();
    emit connectionChanged();
}

void LineEndpointSection::onDirectionArrowClickedInternal()
{
    emit directionArrowClicked();
}

void LineEndpointSection::refreshDirectionArrow()
{
    if (!m_btnDirectionArrow) return;
    if (!m_paramDoc) { m_btnDirectionArrow->setVisible(false); return; }
    auto* block = m_paramDoc->findBlock(m_blockId);
    auto* seg = block ? block->findSegment(m_segmentId) : nullptr;
    if (!block || !seg) { m_btnDirectionArrow->setVisible(false); return; }

    QString why;
    const bool ok = !block->isBridge && !block->isDart() &&
                    block->endTargetPointId.isNull() &&
                    cad::cmd::ReverseSegmentCommand::canReverse(m_paramDoc, m_blockId, m_segmentId, &why);
    m_btnDirectionArrow->setVisible(ok);
    if (!ok) return;

    m_btnDirectionArrow->setText(m_topIsStart ? QString::fromUtf8("↓") : QString::fromUtf8("↑"));
    m_btnDirectionArrow->setToolTip(ok
        ? cad::ui::TooltipFormatter::action(
            QStringLiteral("调换进/出"),
            QStringLiteral("↓ = 上进下出；↑ = 下进上出。点击调换进/出（点固定，几何不变）"))
        : cad::ui::TooltipFormatter::plain(why));
}

} // namespace cad::ui
