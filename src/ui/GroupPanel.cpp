#include "GroupPanel.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include "ElaScrollArea.h"
#include "ElaText.h"
#include "ElaCheckBox.h"
#include "ElaToolButton.h"
#include "ElaPushButton.h"
#include <QMenu>
#include <QInputDialog>
#include <QDrag>
#include <QMimeData>
#include <QApplication>
#include <QStyle>
#include <QDropEvent>
#include <QKeyEvent>
#include <QUndoStack>
#include <QFrame>
#include <QEvent>

#include "IconHelper.h"
#include "parametric/ParamDocument.h"
#include "parametric/Block.h"
#include "parametric/Segment.h"
#include "parametric/Serial.h"
#include "document/commands/GroupCommands.h"

namespace {

/// Drag-drop MIME type for reordering the group list.
constexpr char kGroupMime[] = "application/x-gcad-group";

/// One group card: [checkbox] [caret] [serial tag] [name] [count pill]
/// + a collapsible member preview. The card body click selects the whole
/// group; dragging reorders the list; Enter/Delete work on focus.
class GroupCard : public QFrame
{
    Q_OBJECT

public:
    GroupCard(cad::param::ParamDocument* doc, const QUuid& groupId, QWidget* parent)
        : QFrame(parent)
        , m_doc(doc)
        , m_groupId(groupId)
    {
        setObjectName(QStringLiteral("GroupCard"));
        // Card frame / hover / drop state come from the global theme QSS
        // (objectName + [dropping] attribute selector) — mode-aware.
        setCursor(Qt::PointingHandCursor);
        setContextMenuPolicy(Qt::CustomContextMenu);
        setFocusPolicy(Qt::StrongFocus);
        setAttribute(Qt::WA_StyledBackground, true);

        auto* outer = new QVBoxLayout(this);
        outer->setContentsMargins(8, 6, 10, 6);
        outer->setSpacing(4);

        // ── Row 1: checkbox / caret / tag / name / count ──
        auto* row = new QHBoxLayout();
        row->setSpacing(6);

        m_check = new ElaCheckBox(this);
        m_check->setCursor(Qt::PointingHandCursor);
        connect(m_check, &QCheckBox::toggled, this,
                [this](bool on) { emit checkChanged(m_groupId, on); });
        row->addWidget(m_check);

        m_caret = new ElaToolButton(this);
        m_caret->setCheckable(true);
        m_caret->setCursor(Qt::PointingHandCursor);
        m_caret->setFixedSize(16, 16);
        m_caret->setObjectName(QStringLiteral("groupCaret"));
        m_caret->setIcon(cad::ui::IconHelper::iconByName(
            QStringLiteral("caret-right"), QColor(0x7F, 0x8C, 0x8D)));
        m_caret->setIconSize(QSize(12, 12));
        connect(m_caret, &QToolButton::toggled, this, [this](bool on) {
            m_caret->setIcon(cad::ui::IconHelper::iconByName(
                on ? QStringLiteral("caret-down") : QStringLiteral("caret-right"),
                QColor(0x7F, 0x8C, 0x8D)));
            m_memberBox->setVisible(on);
        });
        row->addWidget(m_caret);

        // Serial tag badge.
        QString serial, name;
        if (const auto* g = doc ? doc->findGroup(groupId) : nullptr) {
            serial = g->serial;
            name = g->name;
        }
        auto* tagLbl = new ElaText(cad::param::Serial::tag(serial), 13, this);
        tagLbl->setObjectName(QStringLiteral("groupCardTag"));
        tagLbl->setToolTip(serial);
        row->addWidget(tagLbl);

        // Group name (or muted placeholder when unnamed).
        auto* nameLbl = new ElaText(QString(), 13, this);
        nameLbl->setObjectName(QStringLiteral("groupCardName"));
        nameLbl->setProperty("dimmed", name.isEmpty());
        nameLbl->setText(name.isEmpty()
            ? QString::fromUtf8("\xe6\x9c\xaa\xe5\x91\xbd\xe5\x90\x8d")  // 未命名
            : name);
        row->addWidget(nameLbl, 1);

        // Member count pill.
        const int count = doc ? doc->blocksInGroup(groupId).size() : 0;
        auto* countLbl = new ElaText(QString::fromUtf8("%1 \xe6\x9d\xa1").arg(count), 13, this);  // N 条
        countLbl->setObjectName(QStringLiteral("groupCardCount"));
        row->addWidget(countLbl);

        // Component hinge status dot (组件主连接 indicator).
        const bool hasHinge = doc && doc->hasComponentHinge(groupId);
        auto* hingeLbl = new ElaText(hasHinge
            ? QString::fromUtf8("\xe9\x93\xbe")  // 链
            : QString::fromUtf8("\xe2\x80\xa2"), // ·
            13, this);
        hingeLbl->setObjectName(QStringLiteral("groupCardHinge"));
        hingeLbl->setToolTip(hasHinge
            ? QString::fromUtf8("\xe5\xb7\xb2\xe8\xae\xbe\xe7\xbd\xae\xe7\xbb\x84\xe4\xbb\xb6\xe4\xb8\xbb\xe8\xbf\x9e\xe6\x8e\xa5")   // 已设置组件主连接
            : QString::fromUtf8("\xe6\x9c\xaa\xe8\xae\xbe\xe7\xbd\xae\xe5\xa5\x97\xe5\x90\x88\xe4\xb8\xbb\xe8\xbf\x9e\xe6\x8e\xa5")); // 未设置主连接
        row->addWidget(hingeLbl);

        // + Add selected blocks to this group.
        auto* addBtn = new ElaToolButton(this);
        addBtn->setFixedSize(22, 22);
        addBtn->setCursor(Qt::PointingHandCursor);
        addBtn->setObjectName(QStringLiteral("groupAddMemberBtn"));
        addBtn->setIcon(cad::ui::IconHelper::iconByName(
            QStringLiteral("plus"), QColor(0x2F, 0x6F, 0xED)));
        addBtn->setIconSize(QSize(14, 14));
        addBtn->setToolTip(QString::fromUtf8(
            "\xe6\xb7\xbb\xe5\x8a\xa0\xe9\x80\x89\xe4\xb8\xad\xe5\x88\xb0\xe7\xbb\x84"));  // 添加选中到组
        connect(addBtn, &QToolButton::clicked, this, [this]() {
            emit addRequested(m_groupId);
        });
        row->addWidget(addBtn);

        // Eye button for bounding box visibility
        bool bBoxVisible = true;
        if (const auto* g = doc ? doc->findGroup(groupId) : nullptr)
            bBoxVisible = g->showBoundingBox;

        auto* eyeBtn = new ElaToolButton(this);
        eyeBtn->setFixedSize(22, 22);
        eyeBtn->setCursor(Qt::PointingHandCursor);
        eyeBtn->setObjectName(QStringLiteral("groupEyeBtn"));
        eyeBtn->setIcon(cad::ui::IconHelper::iconByName(
            bBoxVisible ? QStringLiteral("eye") : QStringLiteral("eye-slash"),
            bBoxVisible ? QColor(0x2F, 0x6F, 0xED) : QColor(0x9A, 0xA4, 0xB2)));
        eyeBtn->setIconSize(QSize(14, 14));
        eyeBtn->setToolTip(bBoxVisible
            ? QString::fromUtf8("\xe9\x9a\x90\xe8\x97\x8f\xe5\x8c\x85\xe5\x9b\xb4\xe6\xa1\x86")   // 隐藏包围框
            : QString::fromUtf8("\xe6\x98\xbe\xe7\xa4\xba\xe5\x8c\x85\xe5\x9b\xb4\xe6\xa1\x86"));  // 显示包围框
        connect(eyeBtn, &QToolButton::clicked, this, [this]() {
            emit eyeClicked(m_groupId);
        });
        row->addWidget(eyeBtn);

        outer->addLayout(row);

        // ── Row 2: member preview (collapsed by default) ──
        m_memberBox = new QWidget(this);
        m_memberLayout = new QVBoxLayout(m_memberBox);
        m_memberLayout->setContentsMargins(26, 2, 4, 2);
        m_memberLayout->setSpacing(2);
        m_memberBox->setVisible(false);
        if (doc) {
            for (const QUuid& memberId : doc->blocksInGroup(groupId)) {
                const cad::param::Block* b = doc->findBlock(memberId);
                if (!b) continue;
                QString segSerial, segName;
                if (!b->segments.empty()) {
                    segSerial = b->segments.front().serial;
                    segName = b->segments.front().name;
                }
                auto* mrow = new QWidget(m_memberBox);
                auto* mlay = new QHBoxLayout(mrow);
                mlay->setContentsMargins(0, 0, 0, 0);
                mlay->setSpacing(6);
                auto* mtag = new ElaText(cad::param::Serial::tag(segSerial), 13, mrow);
                mtag->setObjectName(QStringLiteral("memberTag"));
                mtag->setToolTip(segSerial);
                mlay->addWidget(mtag);
                auto* mname = new ElaText(segName.isEmpty()
                    ? QString::fromUtf8("\xe2\x80\x94") : segName, 13, mrow);  // —
                mname->setObjectName(QStringLiteral("memberName"));
                mlay->addWidget(mname, 1);
                auto* removeBtn = new ElaToolButton(mrow);
                removeBtn->setFixedSize(18, 18);
                removeBtn->setCursor(Qt::PointingHandCursor);
                removeBtn->setObjectName(QStringLiteral("groupMemberRemoveBtn"));
                removeBtn->setIcon(cad::ui::IconHelper::iconByName(
                    QStringLiteral("x"), QColor(0x9A, 0xA4, 0xB2)));
                removeBtn->setIconSize(QSize(12, 12));
                removeBtn->setToolTip(QString::fromUtf8(
                    "\xe5\xb0\x86\xe6\xad\xa4\xe6\x88\x90\xe5\x91\x98\xe7\xa7\xbb\xe5\x87\xba\xe7\xbb\x84"));  // 将此成员移出组
                connect(removeBtn, &QToolButton::clicked, this,
                        [this, memberId]() { emit memberRemoveRequested(m_groupId, memberId); });
                mlay->addWidget(removeBtn);
                m_memberLayout->addWidget(mrow);
            }
        }
        outer->addWidget(m_memberBox);
    }

    [[nodiscard]] QUuid groupId() const { return m_groupId; }

    /// Visual feedback while a drag hovers over this card (reorder target).
    void setDropHighlight(bool on)
    {
        if (m_dropping == on) return;
        m_dropping = on;
        setProperty("dropping", on);
        style()->unpolish(this);
        style()->polish(this);
    }

signals:
    void clicked(const QUuid& groupId);              ///< Card body click / Enter.
    void dissolveRequested(const QUuid& groupId);    ///< Delete key.
    void checkChanged(const QUuid& groupId, bool on);///< Batch checkbox.
    void eyeClicked(const QUuid& groupId);           ///< Bounding box eye toggle.
    void addRequested(const QUuid& groupId);         ///< + button: add selected blocks.
    void memberRemoveRequested(const QUuid& groupId, const QUuid& memberId); ///< − button.

protected:
    void mousePressEvent(QMouseEvent* event) override
    {
        if (event->button() == Qt::LeftButton) {
            m_pressPos = event->pos();
            m_pressed = true;
            m_dragStarted = false;
            setFocus();   // keyboard navigation target
        }
        QFrame::mousePressEvent(event);
    }

    void mouseMoveEvent(QMouseEvent* event) override
    {
        if (m_pressed && !m_dragStarted
            && (event->buttons() & Qt::LeftButton)
            && (event->pos() - m_pressPos).manhattanLength()
                   > QApplication::startDragDistance()) {
            m_dragStarted = true;
            auto* mime = new QMimeData;
            mime->setData(QString::fromLatin1(kGroupMime), m_groupId.toByteArray());
            auto* drag = new QDrag(this);
            drag->setMimeData(mime);
            drag->exec(Qt::MoveAction);
        }
        QFrame::mouseMoveEvent(event);
    }

    void mouseReleaseEvent(QMouseEvent* event) override
    {
        if (m_pressed && !m_dragStarted && event->button() == Qt::LeftButton
            && rect().contains(event->pos()))
            emit clicked(m_groupId);
        m_pressed = false;
        m_dragStarted = false;
        QFrame::mouseReleaseEvent(event);
    }

    void keyPressEvent(QKeyEvent* event) override
    {
        if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter)
            emit clicked(m_groupId);
        else if (event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace)
            emit dissolveRequested(m_groupId);
        QFrame::keyPressEvent(event);
    }

private:
    cad::param::ParamDocument* m_doc = nullptr;
    QUuid m_groupId;
    bool  m_dropping = false;    ///< Drop-target highlight (QSS attribute).
    QCheckBox* m_check = nullptr;
    QToolButton* m_caret = nullptr;
    QWidget* m_memberBox = nullptr;
    QVBoxLayout* m_memberLayout = nullptr;
    QPoint m_pressPos;
    bool m_pressed = false;
    bool m_dragStarted = false;
};

} // namespace

GroupPanel::GroupPanel(cad::param::ParamDocument* doc, QWidget* parent)
    : QWidget(parent)
    , m_doc(doc)
{
    setupUi();

    if (m_doc) {
        // QueuedConnection: groupsChanged can fire from within a command's
        // redo while the panel is mid-refresh — rebuilding synchronously
        // would delete the widgets being iterated (LayerPanel known pitfall).
        connect(m_doc, &cad::param::ParamDocument::groupsChanged,
                this, &GroupPanel::refresh, Qt::QueuedConnection);
        connect(m_doc, &cad::param::ParamDocument::documentReset,
                this, &GroupPanel::refresh, Qt::QueuedConnection);
    }
    refresh();
}

void GroupPanel::setupUi()
{
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    // Header: title + count pill + batch dissolve.
    auto* header = new QWidget(this);
    auto* headerLay = new QHBoxLayout(header);
    headerLay->setContentsMargins(12, 10, 12, 6);
    headerLay->setSpacing(8);
    auto* title = new ElaText(QString::fromUtf8("\xe7\xbb\x84"), 13, header);  // 组
    title->setObjectName(QStringLiteral("groupPanelTitle"));
    headerLay->addWidget(title);
    m_countLabel = new ElaText(QStringLiteral("0"), 13, header);
    m_countLabel->setObjectName(QStringLiteral("groupPanelCount"));
    headerLay->addWidget(m_countLabel);
    headerLay->addStretch(1);
    m_batchBtn = new ElaPushButton(
        QString::fromUtf8("\xe8\xa7\xa3\xe6\x95\xa3\xe9\x80\x89\xe4\xb8\xad"), header);  // 解散选中
    m_batchBtn->setCursor(Qt::PointingHandCursor);
    m_batchBtn->setObjectName(QStringLiteral("groupBatchBtn"));
    m_batchBtn->setEnabled(false);
    connect(m_batchBtn, &QPushButton::clicked, this, &GroupPanel::dissolveCheckedGroups);
    headerLay->addWidget(m_batchBtn);
    outer->addWidget(header);

    // Scrollable card list.
    m_scroll = new ElaScrollArea(this);
    m_scroll->setWidgetResizable(true);
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_container = new QWidget();
    m_container->setAcceptDrops(true);
    m_container->installEventFilter(this);
    m_listLayout = new QVBoxLayout(m_container);
    m_listLayout->setContentsMargins(10, 4, 10, 10);
    m_listLayout->setSpacing(6);
    m_listLayout->addStretch(1);
    m_scroll->setWidget(m_container);
    outer->addWidget(m_scroll, 1);

    // Empty-state hint.
    m_emptyHint = new ElaText(QString::fromUtf8("\xe5\xa4\x9a\xe9\x80\x89\xe7\xba\xbf\xe6\xae\xb5"
                          "\xe5\x90\x8e\xe5\x8f\xb3\xe9\x94\xae\xe7\xa1\xae"
                          "\xe8\xae\xa4\xef\xbc\x8c\xe5\x86\x8d\xe5\x8f\xb3"
                          "\xe9\x94\xae\xe5\x8d\xb3\xe5\x8f\xaf\xe6\x88\x90"
                          "\xe7\xbb\x84"), 13, // 多选线段后右键确认，再右键即可成组
        m_container);
    m_emptyHint->setWordWrap(true);
    m_emptyHint->setAlignment(Qt::AlignHCenter | Qt::AlignTop);
    m_emptyHint->setObjectName(QStringLiteral("groupEmptyHint"));
    m_listLayout->addWidget(m_emptyHint);
}

void GroupPanel::refresh()
{
    if (!m_doc || !m_listLayout) return;

    // Drop the drop-target highlight (cards may be stale) and every card.
    m_dropTarget = nullptr;
    while (m_listLayout->count() > 0) {
        QLayoutItem* item = m_listLayout->takeAt(0);
        if (QWidget* w = item->widget()) {
            if (w != m_emptyHint)
                w->deleteLater();
        }
        delete item;
    }
    m_listLayout->addWidget(m_emptyHint);
    m_cards.clear();
    m_checked.clear();
    updateBatchButton();

    const auto& groups = m_doc->groups();
    m_countLabel->setText(QString::number(groups.size()));
    m_emptyHint->setVisible(groups.empty());

    for (const auto& g : groups) {
        auto* card = new GroupCard(m_doc, g.id, m_container);
        connect(card, &GroupCard::clicked, this, [this, gid = g.id] {
            emit selectGroupRequested(m_doc->blocksInGroup(gid));
        });
        connect(card, &GroupCard::dissolveRequested, this,
                [this](const QUuid& gid) { dissolveGroup(gid); });
        connect(card, &GroupCard::eyeClicked, this, [this](const QUuid& gid) {
            if (!m_doc) return;
            const bool cur = m_doc->isGroupBoundingBoxVisible(gid);
            if (m_undoStack)
                m_undoStack->push(new cad::cmd::SetGroupBoundingBoxCommand(m_doc, gid, !cur));
            else
                m_doc->setGroupBoundingBoxVisible(gid, !cur);
        });
        connect(card, &GroupCard::addRequested, this,
                [this](const QUuid& gid) { emit addMembersRequested(gid); });
        connect(card, &GroupCard::memberRemoveRequested, this,
                [this](const QUuid& gid, const QUuid& memberId) {
                    removeMembersFromGroup(gid, {memberId});
                });
        connect(card, &GroupCard::checkChanged, this, [this](const QUuid& gid, bool on) {
            if (on) m_checked.insert(gid);
            else    m_checked.remove(gid);
            updateBatchButton();
        });
        connect(card, &QWidget::customContextMenuRequested, this,
                [card, this, gid = g.id](const QPoint& p) {
                    showGroupMenu(card->mapToGlobal(p), gid);
                });
        m_listLayout->addWidget(card);
        m_cards.append(card);
    }
    m_listLayout->addStretch(1);
}

bool GroupPanel::eventFilter(QObject* watched, QEvent* event)
{
    // ── Drag-drop on the container: reorder the group list. ──
    if (watched == m_container) {
        switch (event->type()) {
        case QEvent::DragEnter: {
            auto* de = static_cast<QDragEnterEvent*>(event);
            if (de->mimeData()->hasFormat(QString::fromLatin1(kGroupMime))) {
                de->acceptProposedAction();
                return true;
            }
            return false;
        }
        case QEvent::DragMove: {
            auto* dm = static_cast<QDragMoveEvent*>(event);
            if (!dm->mimeData()->hasFormat(QString::fromLatin1(kGroupMime)))
                return false;
            QWidget* target = cardAt(dm->position().toPoint());
            if (target != m_dropTarget) {
                if (auto* c = qobject_cast<GroupCard*>(m_dropTarget))
                    c->setDropHighlight(false);
                m_dropTarget = target;
                if (auto* c = qobject_cast<GroupCard*>(m_dropTarget))
                    c->setDropHighlight(true);
            }
            dm->acceptProposedAction();
            return true;
        }
        case QEvent::DragLeave:
            if (auto* c = qobject_cast<GroupCard*>(m_dropTarget))
                c->setDropHighlight(false);
            m_dropTarget = nullptr;
            return false;
        case QEvent::Drop: {
            auto* de = static_cast<QDropEvent*>(event);
            if (!de->mimeData()->hasFormat(QString::fromLatin1(kGroupMime)))
                return false;
            if (auto* c = qobject_cast<GroupCard*>(m_dropTarget))
                c->setDropHighlight(false);
            m_dropTarget = nullptr;

            const QUuid gid = QUuid::fromString(
                QString::fromUtf8(de->mimeData()->data(QString::fromLatin1(kGroupMime))));
            const QWidget* target = cardAt(de->position().toPoint());
            int fromIdx = -1, toIdx = -1;
            for (int i = 0; i < m_cards.size(); ++i) {
                if (qobject_cast<GroupCard*>(m_cards[i])->groupId() == gid)
                    fromIdx = i;
                if (m_cards[i] == target)
                    toIdx = i;
            }
            if (fromIdx >= 0 && toIdx >= 0 && fromIdx != toIdx && m_doc) {
                // Undoable: the order is persisted, so a plain moveGroup
                // would silently break the dirty flag (save → drag → close).
                if (m_undoStack)
                    m_undoStack->push(new cad::cmd::MoveGroupCommand(
                        m_doc, fromIdx, toIdx));
                else
                    m_doc->moveGroup(fromIdx, toIdx);
            }   // groupsChanged → refresh
            de->acceptProposedAction();
            return true;
        }
        default:
            break;
        }
    }
    return QWidget::eventFilter(watched, event);
}

QWidget* GroupPanel::cardAt(const QPoint& pos) const
{
    for (QWidget* card : m_cards) {
        if (card->isVisible() && card->geometry().contains(pos))
            return card;
    }
    return nullptr;
}

void GroupPanel::showGroupMenu(const QPoint& globalPos, const QUuid& groupId)
{
    if (!m_doc) return;
    const bool isBBoxVisible = m_doc->isGroupBoundingBoxVisible(groupId);
    const bool hasHinge = m_doc->hasComponentHinge(groupId);

    QMenu menu;
    QAction* actAddSelected = menu.addAction(QString::fromUtf8("\xe6\xb7\xbb\xe5\x8a\xa0\xe9\x80\x89\xe4\xb8\xad\xe5\x88\xb0\xe7\xbb\x84")); // 添加选中到组
    QAction* actClearHinge  = menu.addAction(QString::fromUtf8("\xe6\xb8\x85\xe9\x99\xa4\xe7\xbb\x84\xe4\xbb\xb6\xe4\xb8\xbb\xe8\xbf\x9e\xe6\x8e\xa5")); // 清除组件主连接
    actClearHinge->setEnabled(hasHinge);
    menu.addSeparator();
    QAction* actRename   = menu.addAction(QString::fromUtf8("\xe9\x87\x8d\xe5\x91\xbd\xe5\x90\x8d"));    // 重命名
    QAction* actBBox     = menu.addAction(isBBoxVisible
        ? QString::fromUtf8("\xe9\x9a\x90\xe8\x97\x8f\xe5\x8c\x85\xe5\x9b\xb4\xe6\xa1\x86")  // 隐藏包围框
        : QString::fromUtf8("\xe6\x98\xbe\xe7\xa4\xba\xe5\x8c\x85\xe5\x9b\xb4\xe6\xa1\x86")); // 显示包围框
    QAction* actDissolve = menu.addAction(QString::fromUtf8("\xe8\xa7\xa3\xe6\x95\xa3\xe7\xbb\x84"));    // 解散组
    QAction* picked = menu.exec(globalPos);
    if (picked == actAddSelected)     emit addMembersRequested(groupId);
    else if (picked == actClearHinge && hasHinge) {
        if (m_undoStack)
            m_undoStack->push(new cad::cmd::ClearComponentHingeCommand(m_doc, groupId));
        else
            m_doc->clearComponentHinge(groupId);
    }
    else if (picked == actRename)          renameGroup(groupId);
    else if (picked == actBBox) {
        if (m_undoStack)
            m_undoStack->push(new cad::cmd::SetGroupBoundingBoxCommand(m_doc, groupId, !isBBoxVisible));
        else
            m_doc->setGroupBoundingBoxVisible(groupId, !isBBoxVisible);
    }
    else if (picked == actDissolve)   dissolveGroup(groupId);
}

void GroupPanel::renameGroup(const QUuid& groupId)
{
    if (!m_doc) return;
    const auto* g = m_doc->findGroup(groupId);
    if (!g) return;
    bool ok = false;
    const QString name = QInputDialog::getText(
        this,
        QString::fromUtf8("\xe9\x87\x8d\xe5\x91\xbd\xe5\x90\x8d\xe7\xbb\x84"),   // 重命名组
        QString::fromUtf8("\xe7\xbb\x84\xe5\x90\x8d\xe7\xa7\xb0"),               // 组名称
        QLineEdit::Normal, g->name, &ok);
    if (ok) {
        if (m_undoStack)
            m_undoStack->push(new cad::cmd::RenameGroupCommand(m_doc, groupId, name.trimmed()));
        else
            m_doc->setGroupName(groupId, name.trimmed());
    }
}

void GroupPanel::dissolveGroup(const QUuid& groupId)
{
    if (!m_doc) return;
    if (m_undoStack)
        m_undoStack->push(new cad::cmd::UngroupCommand(m_doc, groupId));
    else
        m_doc->dissolveGroup(groupId);
}

void GroupPanel::addMembersToGroup(const QUuid& groupId, const QList<QUuid>& memberIds)
{
    if (!m_doc || memberIds.isEmpty()) return;
    if (m_undoStack)
        m_undoStack->push(new cad::cmd::AddGroupMembersCommand(m_doc, groupId, memberIds));
    else
        for (const QUuid& id : memberIds)
            m_doc->addGroupMember(groupId, id);
}

void GroupPanel::removeMembersFromGroup(const QUuid& groupId, const QList<QUuid>& memberIds)
{
    if (!m_doc || memberIds.isEmpty()) return;
    if (m_undoStack)
        m_undoStack->push(new cad::cmd::RemoveGroupMembersCommand(m_doc, groupId, memberIds));
    else
        for (const QUuid& id : memberIds)
            m_doc->removeGroupMember(groupId, id);
}

void GroupPanel::dissolveCheckedGroups()
{
    if (!m_doc || m_checked.isEmpty()) return;
    if (m_undoStack) {
        // One undo step for the whole batch (批量解散).
        m_undoStack->beginMacro(
            QString::fromUtf8("\xe8\xa7\xa3\xe6\x95\xa3\xe9\x80\x89\xe4\xb8\xad\xe7\xbb\x84"));  // 解散选中组
        for (const QUuid& gid : m_checked)
            m_undoStack->push(new cad::cmd::UngroupCommand(m_doc, gid));
        m_undoStack->endMacro();
    } else {
        for (const QUuid& gid : m_checked)
            m_doc->dissolveGroup(gid);
    }
    m_checked.clear();
    updateBatchButton();
}

void GroupPanel::updateBatchButton()
{
    if (m_batchBtn)
        m_batchBtn->setEnabled(!m_checked.isEmpty());
}

#include "GroupPanel.moc"
