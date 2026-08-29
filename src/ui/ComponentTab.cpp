#include "ComponentTab.h"

#include <QScrollArea>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QPushButton>
#include <QFrame>
#include <QMessageBox>
#include <QUndoStack>

#include "document/commands/AttachmentCommands.h"
#include "geometry/Angle.h"

#include "ElaScrollArea.h"
#include "ElaText.h"
#include "ElaLineEdit.h"

#include "parametric/ParamDocument.h"
#include "parametric/Component.h"
#include "parametric/ConditionEngine.h"
#include "document/commands/ComponentCommands.h"
#include "ui/Theme.h"
#include "ui/FormScaffold.h"

namespace cad::ui {

namespace {
constexpr int kActionBtnH = 22;  ///< 紧凑操作按钮高度.
}

ComponentTab::ComponentTab(cad::param::ParamDocument* doc, QWidget* parent)
    : QWidget(parent)
    , m_doc(doc)
{
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    auto* scroll = new ElaScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setObjectName(QStringLiteral("cardListArea"));
    outer->addWidget(scroll, 1);

    m_rowsContainer = new QWidget();
    m_rowsContainer->setObjectName(QStringLiteral("cardListContainer"));
    m_rowsLayout = new QVBoxLayout(m_rowsContainer);
    m_rowsLayout->setContentsMargins(8, 8, 8, 8);
    m_rowsLayout->setSpacing(6);
    m_rowsLayout->addStretch();
    scroll->setWidget(m_rowsContainer);

    applyTheme();

    // Queued: avoid rebuilding (and deleting) a row widget during its own
    // signal emission (name editingFinished → command → componentsChanged).
    connect(m_doc, &cad::param::ParamDocument::componentsChanged,
            this, &ComponentTab::sync, Qt::QueuedConnection);
    // 对接角度随连接建立/断开变化 → 刷新. 手势建立的连接走
    // AddAttachmentCommand.redo → addAttachmentRaw (静默), 不触发
    // structureChanged/componentsChanged — documentChanged (resolveAll 发出)
    // 是可靠的结构变化信号.
    connect(m_doc, &cad::param::ParamDocument::structureChanged,
            this, &ComponentTab::sync, Qt::QueuedConnection);
    connect(m_doc, &cad::param::ParamDocument::documentChanged,
            this, &ComponentTab::sync, Qt::QueuedConnection);

    sync();
}

ComponentTab::~ComponentTab() = default;

void ComponentTab::setUndoStack(QUndoStack* stack)
{
    m_undoStack = stack;
}

void ComponentTab::applyTheme()
{
    const auto& tok = cad::ui::Theme::tokens();
    const QString scrollQss = QStringLiteral(
        "QScrollArea { background: %1; border: none; }")
        .arg(tok.canvasBg.name());
    const QString containerQss = QStringLiteral("background: %1;")
        .arg(tok.canvasBg.name());
    for (ElaScrollArea* sa : findChildren<ElaScrollArea*>(QStringLiteral("cardListArea")))
        sa->setStyleSheet(scrollQss);
    for (QWidget* c : findChildren<QWidget*>(QStringLiteral("cardListContainer")))
        c->setStyleSheet(containerQss);

    // 卡片: 圆角 + 凹陷表面 (surface2 区别于画布纸色底), 紧凑操作按钮.
    // 圆角纪律 (ui-redesign §07): 功能圆角上限 4px, 原 6px 钳到 4。
    const QString cardQss = QStringLiteral(
        "QWidget#componentCard { background: %1; border-radius: 4px; }"
        "QPushButton#componentActionBtn {"
        "  background: %2; border: 1px solid %3; border-radius: 4px;"
        "  padding: 2px 8px; font-size: 11px;"
        "}"
        "QPushButton#componentActionBtn:hover { background: %4; }"
        "QPushButton#componentActionBtn:disabled { color: %5; }")
        .arg(tok.surface2.name(), tok.surface.name(), tok.border.name(),
             tok.surface3.name(), tok.text3.name());
    for (QWidget* w : findChildren<QWidget*>(QStringLiteral("componentCard")))
        w->setStyleSheet(cardQss);
}

void ComponentTab::sync()
{
    rebuild();
}

void ComponentTab::rebuild()
{
    // Drop every row widget, keep the trailing stretch.
    while (m_rowsLayout->count() > 1) {
        QLayoutItem* item = m_rowsLayout->takeAt(0);
        if (item->widget())
            item->widget()->deleteLater();
        delete item;
    }

    const auto& comps = m_doc->components();
    if (comps.empty()) {
        auto* hint = new ElaText(
            QStringLiteral("暂无组件\n在画布上多选（≥2 条线段）后右键 →「创建组件」\n"
                           "组件 = 整体移动单元：拖任一成员整组移动\n"
                           "（线级连接默认焊接拖不拆；多线整体联动可交给组件）"),
            13, m_rowsContainer);
        hint->setAlignment(Qt::AlignCenter);
        hint->setObjectName(QStringLiteral("dimText"));
        m_rowsLayout->insertWidget(0, hint);
        return;
    }

    const auto& tok = cad::ui::Theme::tokens();
    int index = 0;
    for (const auto& c : comps) {
        auto* card = new QWidget(m_rowsContainer);
        card->setObjectName(QStringLiteral("componentCard"));
        auto* h = new QHBoxLayout(card);
        h->setContentsMargins(0, 0, 0, 0);
        h->setSpacing(0);

        // 左侧竖线 = 组件类型色 piece1 碳灰 (方案 A: 竖线标类型, 不再蓝橙交替).
        auto* bar = new QFrame(card);
        bar->setObjectName(QStringLiteral("componentCardBar"));
        bar->setFixedWidth(3);
        bar->setStyleSheet(QStringLiteral(
            "background: %1; border: none; border-radius: 1px;")
            .arg(tok.piece1.name()));
        h->addWidget(bar);

        auto* body = new QWidget(card);
        auto* v = new QVBoxLayout(body);
        v->setContentsMargins(8, 6, 8, 6);
        v->setSpacing(4);

        // 行1: 序号 + 名称(可编辑) + 成员数.
        auto* r1 = new QHBoxLayout();
        r1->setSpacing(6);
        auto* idx = new QLabel(
            QStringLiteral("组件 %1").arg(index + 1), body);
        idx->setObjectName(QStringLiteral("componentIndex"));
        auto* name = new QLineEdit(c.name, body);
        name->setObjectName(QStringLiteral("componentNameEdit"));
        auto* count = new QLabel(
            QStringLiteral("%1 条").arg(static_cast<int>(c.memberBlockIds.size())), body);
        count->setObjectName(QStringLiteral("componentCount"));
        r1->addWidget(idx);
        r1->addWidget(name, 1);
        r1->addWidget(count);
        v->addLayout(r1);

        // 行2: 原始角 (回正目标) — 独占一行, 支持数值或公式.
        auto* r2 = new QHBoxLayout();
        r2->setSpacing(6);
        auto* origLabel = new QLabel(QStringLiteral("原始角"), body);
        auto* origEdit = new ElaLineEdit(body);
        origEdit->setMinimumWidth(76);
        origEdit->setPlaceholderText(kPlaceholderAngleOrFormula);
        origEdit->setToolTip(QStringLiteral("组的原始角度（「回正」的目标），可填数值或公式"));
        origEdit->setText(c.defaultAngleFormula.isEmpty()
            ? QString::number(cad::geo::normalizeDeg360(c.defaultAngleDeg), 'f', 1)
            : c.defaultAngleFormula);
        auto* origFx = new ElaText(
            QStringLiteral("<i style='color:%1;'>fx</i>").arg(tok.text2.name()), 13, body);
        origFx->setFixedWidth(18);
        origFx->setVisible(!c.defaultAngleFormula.isEmpty());
        auto* origVal = new ElaText(QString(), 13, body);
        origVal->setObjectName(QStringLiteral("dimText"));
        origVal->setStyleSheet("font-size:11px;");
        if (!c.defaultAngleFormula.isEmpty()) {
            auto rr = cad::param::ConditionEngine::evaluate(
                c.defaultAngleFormula, m_doc->parameters(), m_doc->conditions());
            if (rr.ok) origVal->setText(QStringLiteral("= %1°").arg(rr.value, 0, 'f', 1));
        }
        r2->addWidget(origLabel);
        r2->addWidget(origEdit, 1);
        r2->addWidget(origFx);
        r2->addWidget(origVal);
        v->addLayout(r2);

        // 行3: 对接角 (外部跟随角) — 独占一行, 支持数值或公式.
        auto* r2b = new QHBoxLayout();
        r2b->setSpacing(6);
        auto* dockLabel = new QLabel(QStringLiteral("对接角"), body);
        auto* dockEdit = new ElaLineEdit(body);
        dockEdit->setMinimumWidth(76);
        dockEdit->setPlaceholderText(kPlaceholderAngleOrFormula);
        dockEdit->setEnabled(false);
        dockEdit->setToolTip(QStringLiteral("组对接外部线的跟随角（0°=折叠、180°=沿外部线直行），可填数值或公式"));
        auto* dockFx = new ElaText(
            QStringLiteral("<i style='color:%1;'>fx</i>").arg(tok.text2.name()), 13, body);
        dockFx->setFixedWidth(18);
        dockFx->setVisible(false);
        auto* dockVal = new ElaText(QString(), 13, body);
        dockVal->setObjectName(QStringLiteral("dimText"));
        dockVal->setStyleSheet("font-size:11px;");

        QUuid compAttId;
        for (const auto& a : m_doc->attachments()) {
            if (a.fromComponentId == c.id) {
                compAttId = a.id;
                dockEdit->setEnabled(true);
                if (a.followerAngleFormula.isEmpty()) {
                    dockEdit->setText(QString::number(
                        cad::geo::normalizeDeg360(a.followerAngle), 'f', 1));
                } else {
                    dockEdit->setText(a.followerAngleFormula);
                    dockFx->setVisible(true);
                    auto rr = cad::param::ConditionEngine::evaluate(
                        a.followerAngleFormula, m_doc->parameters(), m_doc->conditions());
                    if (rr.ok) dockVal->setText(QStringLiteral("= %1°").arg(rr.value, 0, 'f', 1));
                }
                break;
            }
        }
        r2b->addWidget(dockLabel);
        r2b->addWidget(dockEdit, 1);
        r2b->addWidget(dockFx);
        r2b->addWidget(dockVal);
        v->addLayout(r2b);

        // 行4: 操作 (包围盒 / 回正 / 断开 / 解散 / 删除).
        auto* r3 = new QHBoxLayout();
        r3->setSpacing(6);
        auto* bbox = new QCheckBox(QStringLiteral("包围盒"), body);
        bbox->setChecked(c.showBoundingBox);
        auto* resetBtn = new QPushButton(QStringLiteral("回正"), body);
        auto* detachBtn = new QPushButton(QStringLiteral("断开"), body);
        auto* dissolveBtn = new QPushButton(QStringLiteral("解散"), body);
        auto* delBtn = new QPushButton(QStringLiteral("删除"), body);
        for (auto* btn : {resetBtn, detachBtn, dissolveBtn, delBtn}) {
            btn->setObjectName(QStringLiteral("componentActionBtn"));
            btn->setFixedHeight(kActionBtnH);
            btn->setCursor(Qt::PointingHandCursor);
        }
        resetBtn->setToolTip(QStringLiteral("回到初始状态（断开对接 + 转回原始角 + 清除公式）"));
        detachBtn->setToolTip(QStringLiteral("断开组对接外部线的连接"));
        detachBtn->setVisible(!compAttId.isNull());

        r3->addWidget(bbox);
        r3->addStretch();
        r3->addWidget(resetBtn);
        r3->addWidget(detachBtn);
        r3->addWidget(dissolveBtn);
        r3->addWidget(delBtn);
        v->addLayout(r3);

        h->addWidget(body, 1);

        const QUuid compId = c.id;

        connect(name, &QLineEdit::editingFinished, this, [this, compId, name]() {
            const auto* comp = m_doc->componentsView().byId(compId);
            if (!comp || comp->name == name->text())
                return;
            if (m_undoStack)
                m_undoStack->push(new cad::cmd::SetComponentPropertyCommand(
                    m_doc, compId, name->text(), comp->showBoundingBox,
                    comp->defaultAngleDeg, comp->defaultAngleFormula));
        });

        connect(bbox, &QCheckBox::toggled, this, [this, compId](bool on) {
            const auto* comp = m_doc->componentsView().byId(compId);
            if (!comp || comp->showBoundingBox == on)
                return;
            if (m_undoStack)
                m_undoStack->push(new cad::cmd::SetComponentPropertyCommand(
                    m_doc, compId, comp->name, on, comp->defaultAngleDeg, comp->defaultAngleFormula));
        });

        connect(origEdit, &ElaLineEdit::editingFinished, this,
                [this, compId, origEdit]() {
                    const auto* comp = m_doc->componentsView().byId(compId);
                    if (!comp) return;
                    const QString text = origEdit->text().trimmed();
                    bool isNum = false;
                    const double num = text.toDouble(&isNum);
                    const QString formula = isNum ? QString() : text;
                    const double deg = isNum ? cad::geo::normalizeDeg360(num)
                                             : comp->defaultAngleDeg;
                    if (isNum && comp->defaultAngleFormula.isEmpty()
                        && std::abs(comp->defaultAngleDeg - deg) < 1e-6)
                        return;
                    if (!isNum && comp->defaultAngleFormula == text)
                        return;
                    if (m_undoStack)
                        m_undoStack->push(new cad::cmd::SetComponentPropertyCommand(
                            m_doc, compId, comp->name, comp->showBoundingBox, deg, formula));
                });

        connect(dockEdit, &ElaLineEdit::editingFinished, this,
                [this, compAttId, dockEdit]() {
                    if (compAttId.isNull() || !m_undoStack) return;
                    const auto* att = m_doc->findAttachment(compAttId);
                    if (!att) return;
                    const QString text = dockEdit->text().trimmed();
                    bool isNum = false;
                    const double num = text.toDouble(&isNum);
                    const QString formula = isNum ? QString() : text;
                    const double deg = isNum ? cad::geo::normalizeDeg360(num)
                                             : att->followerAngle;
                    m_undoStack->push(new cad::cmd::SetFollowerAngleCommand(
                        m_doc, compAttId, deg, formula));
                });

        connect(resetBtn, &QPushButton::clicked, this, [this, compId]() {
            if (m_undoStack)
                m_undoStack->push(new cad::cmd::ResetComponentAngleCommand(m_doc, compId));
        });

        connect(detachBtn, &QPushButton::clicked, this, [this, compAttId]() {
            if (compAttId.isNull() || !m_undoStack) return;
            m_undoStack->push(new cad::cmd::RemoveAttachmentCommand(m_doc, compAttId));
        });

        connect(dissolveBtn, &QPushButton::clicked, this, [this, compId]() {
            if (m_undoStack)
                m_undoStack->push(new cad::cmd::DissolveComponentCommand(m_doc, compId));
        });

        connect(delBtn, &QPushButton::clicked, this, [this, compId]() {
            const auto* comp = m_doc->componentsView().byId(compId);
            if (!comp) return;
            const auto ret = QMessageBox::question(
                this, QStringLiteral("删除组件"),
                QStringLiteral("删除组件「%1」将连同 %2 条成员线段一起删除，是否继续？")
                    .arg(comp->name).arg(static_cast<int>(comp->memberBlockIds.size())));
            if (ret != QMessageBox::Yes) return;
            if (m_undoStack)
                m_undoStack->push(new cad::cmd::DeleteComponentCommand(m_doc, compId));
        });

        m_rowsLayout->insertWidget(index++, card);
    }
}

} // namespace cad::ui
