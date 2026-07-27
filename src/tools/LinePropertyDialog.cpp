#include "LinePropertyDialog.h"

#include <algorithm>

#include <QTabWidget>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QLineEdit>
#include <QComboBox>
#include <QCheckBox>
#include <QLabel>
#include <QDoubleSpinBox>
#include <QDialogButtonBox>
#include <QGroupBox>
#include <QPushButton>
#include <QFrame>
#include <QMouseEvent>

#include "parametric/ParamDocument.h"
#include "parametric/Block.h"
#include "parametric/Serial.h"
#include "canvas/CanvasScene.h"
#include "geometry/Units.h"

namespace cad::tools {

namespace {

/// One connection at an endpoint of the current segment.
struct ConnEntry {
    bool    isLeader = false;  ///< true = related segment is the leader (被捕捉).
    QString segSerial;
    QString segName;
    QString pointSerial;
    QString pointName;
    double  angle = 0.0;
    QUuid   blockId;
    QUuid   segmentId;
};

/// Clickable bubble card describing one connection. Single-click selects the
/// related segment on canvas; double-click jumps the dialog to edit it.
class ConnCard : public QFrame
{
public:
    ConnCard(const ConnEntry& e, CanvasScene* scene,
             LinePropertyDialog* dlg, QWidget* parent)
        : QFrame(parent), m_scene(scene), m_dlg(dlg)
        , m_blockId(e.blockId), m_segmentId(e.segmentId)
    {
        setStyleSheet(QStringLiteral(
            "border:1px solid #c9d6e0; border-radius:5px; background:#f7fafc;"));
        setCursor(Qt::PointingHandCursor);

        const QString role = e.isLeader ? QString::fromUtf8("被捕捉")
                                        : QString::fromUtf8("捕捉");
        const QString roleColor = e.isLeader ? QStringLiteral("#0078d7")
                                             : QStringLiteral("#b8860b");
        const QString html = QStringLiteral(
            "<div style='margin:2px;'>"
            "<div><b style='color:%1;'>[%2]</b> %3 &nbsp;%4</div>"
            "<div style='color:#555;'>点 %5 &middot; %6 &middot; &ang;%7&deg;</div>"
            "</div>")
            .arg(roleColor, role,
                 cad::param::Serial::toHtml(e.segSerial),
                 e.segName.isEmpty() ? QStringLiteral("—") : e.segName.toHtmlEscaped(),
                 cad::param::Serial::toHtml(e.pointSerial),
                 e.pointName.isEmpty() ? QStringLiteral("—") : e.pointName.toHtmlEscaped(),
                 QString::number(e.angle, 'f', 1));

        auto* label = new QLabel(html, this);
        label->setTextFormat(Qt::RichText);
        label->setAttribute(Qt::WA_TransparentForMouseEvents, true);
        auto* lay = new QVBoxLayout(this);
        lay->setContentsMargins(5, 3, 5, 3);
        lay->addWidget(label);
    }

protected:
    void mousePressEvent(QMouseEvent*) override
    {
        if (m_scene && !m_blockId.isNull())
            m_scene->selectBlock(m_blockId);
    }
    void mouseDoubleClickEvent(QMouseEvent*) override
    {
        if (m_dlg && !m_blockId.isNull() && !m_segmentId.isNull())
            m_dlg->setTarget(m_blockId, m_segmentId);
    }

private:
    CanvasScene* m_scene;
    LinePropertyDialog* m_dlg;
    QUuid m_blockId;
    QUuid m_segmentId;
};

} // namespace

LinePropertyDialog::LinePropertyDialog(const QUuid& blockId, const QUuid& segmentId,
                                       cad::param::ParamDocument* paramDoc,
                                       CanvasScene* scene,
                                       QWidget* parent)
    : QDialog(parent)
    , m_blockId(blockId)
    , m_segmentId(segmentId)
    , m_paramDoc(paramDoc)
    , m_scene(scene)
{
    setWindowTitle(QString::fromUtf8("线段属性"));
    setMinimumWidth(380);

    auto* mainLayout = new QVBoxLayout(this);

    auto* tabs = new QTabWidget(this);
    buildPage1(tabs);
    buildPage2(tabs);
    mainLayout->addWidget(tabs);

    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    mainLayout->addWidget(buttons);

    connect(buttons, &QDialogButtonBox::accepted, this, &LinePropertyDialog::onAccepted);
    connect(buttons, &QDialogButtonBox::rejected, this, &LinePropertyDialog::onRejected);

    populateFromModel();
    connectLiveSignals();
}

void LinePropertyDialog::connectLiveSignals()
{
    // Live update for non-length fields
    connect(m_editName,      &QLineEdit::textChanged,        this, &LinePropertyDialog::onLiveUpdate);
    connect(m_chkShowName,   &QCheckBox::toggled,            this, &LinePropertyDialog::onLiveUpdate);
    connect(m_chkShowLength, &QCheckBox::toggled,            this, &LinePropertyDialog::onLiveUpdate);
    connect(m_editStartName, &QLineEdit::textChanged,        this, &LinePropertyDialog::onLiveUpdate);
    connect(m_chkShowStartName, &QCheckBox::toggled,         this, &LinePropertyDialog::onLiveUpdate);
    connect(m_editEndName,   &QLineEdit::textChanged,        this, &LinePropertyDialog::onLiveUpdate);
    connect(m_chkShowEndName, &QCheckBox::toggled,           this, &LinePropertyDialog::onLiveUpdate);
    connect(m_cmbStyle,      &QComboBox::currentIndexChanged, this, &LinePropertyDialog::onLiveUpdate);
    connect(m_spinWeight,    &QDoubleSpinBox::valueChanged,   this, &LinePropertyDialog::onLiveUpdate);
    connect(m_spinAngle,     &QDoubleSpinBox::valueChanged,   this, &LinePropertyDialog::onLiveUpdate);
    connect(m_btnDetach,     &QPushButton::clicked,           this, &LinePropertyDialog::onDetach);

    // Length: button-triggered refresh (not live)
    connect(m_editLength,    &QLineEdit::textChanged,        this, &LinePropertyDialog::onLengthTextChanged);
    connect(m_btnLenRefresh, &QPushButton::clicked,          this, &LinePropertyDialog::onLengthRefresh);
}

void LinePropertyDialog::buildPage1(QTabWidget* tabs)
{
    auto* page = new QWidget(this);
    auto* layout = new QFormLayout(page);

    m_lblSegId = new QLabel(page);
    m_lblSegId->setTextFormat(Qt::RichText);
    layout->addRow(QString::fromUtf8("线段ID:"), m_lblSegId);

    m_editName = new QLineEdit(page);
    m_editName->setPlaceholderText(QString::fromUtf8("可选名称"));
    layout->addRow(QString::fromUtf8("名称:"), m_editName);

    m_chkShowName = new QCheckBox(QString::fromUtf8("在画布上显示名称"), page);
    layout->addRow(QString(), m_chkShowName);

    // Length in cm with refresh button
    auto* lenRow = new QHBoxLayout();
    m_editLength = new QLineEdit(page);
    m_editLength->setPlaceholderText(QString::fromUtf8("数值(cm)或表达式，如 b/4+0.6"));
    m_btnLenRefresh = new QPushButton(QString::fromUtf8("刷新"), page);
    m_btnLenRefresh->setFixedWidth(50);
    m_btnLenRefresh->setEnabled(false);
    lenRow->addWidget(m_editLength);
    lenRow->addWidget(m_btnLenRefresh);
    layout->addRow(QString::fromUtf8("长度(cm):"), lenRow);

    m_chkShowLength = new QCheckBox(QString::fromUtf8("在画布上显示长度"), page);
    layout->addRow(QString(), m_chkShowLength);

    // Start point group
    auto* grpStart = new QGroupBox(QString::fromUtf8("起点"), page);
    auto* startLayout = new QFormLayout(grpStart);
    m_lblStartPtId = new QLabel(grpStart);
    m_lblStartPtId->setTextFormat(Qt::RichText);
    startLayout->addRow("ID:", m_lblStartPtId);
    m_editStartName = new QLineEdit(grpStart);
    startLayout->addRow(QString::fromUtf8("名称:"), m_editStartName);
    m_chkShowStartName = new QCheckBox(QString::fromUtf8("在画布上显示名称"), grpStart);
    startLayout->addRow(QString(), m_chkShowStartName);
    m_editStartAnno = new QLineEdit(grpStart);
    startLayout->addRow(QString::fromUtf8("注释:"), m_editStartAnno);
    layout->addRow(grpStart);

    // End point group
    auto* grpEnd = new QGroupBox(QString::fromUtf8("终点"), page);
    auto* endLayout = new QFormLayout(grpEnd);
    m_lblEndPtId = new QLabel(grpEnd);
    m_lblEndPtId->setTextFormat(Qt::RichText);
    endLayout->addRow("ID:", m_lblEndPtId);
    m_editEndName = new QLineEdit(grpEnd);
    endLayout->addRow(QString::fromUtf8("名称:"), m_editEndName);
    m_chkShowEndName = new QCheckBox(QString::fromUtf8("在画布上显示名称"), grpEnd);
    endLayout->addRow(QString(), m_chkShowEndName);
    m_editEndAnno = new QLineEdit(grpEnd);
    endLayout->addRow(QString::fromUtf8("注释:"), m_editEndAnno);
    layout->addRow(grpEnd);

    // Line style
    m_cmbStyle = new QComboBox(page);
    m_cmbStyle->addItem(QString::fromUtf8("实线"));
    m_cmbStyle->addItem(QString::fromUtf8("虚线"));
    m_cmbStyle->addItem(QString::fromUtf8("点线"));
    layout->addRow(QString::fromUtf8("线型:"), m_cmbStyle);

    // Line weight
    m_spinWeight = new QDoubleSpinBox(page);
    m_spinWeight->setRange(0.5, 10.0);
    m_spinWeight->setSingleStep(0.2);
    m_spinWeight->setSuffix(" px");
    layout->addRow(QString::fromUtf8("线宽:"), m_spinWeight);

    tabs->addTab(page, QString::fromUtf8("基本"));
}

void LinePropertyDialog::buildPage2(QTabWidget* tabs)
{
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);

    // --- Endpoint header: 起点 ——— 终点 ---
    auto* headerRow = new QHBoxLayout();
    m_lblStartHeader = new QLabel(page);
    m_lblStartHeader->setTextFormat(Qt::RichText);
    m_lblStartHeader->setAlignment(Qt::AlignCenter);
    m_lblStartHeader->setStyleSheet(
        "border:1px solid #b9cfe0; border-radius:4px; padding:4px; background:#f4f8fb;");
    headerRow->addWidget(m_lblStartHeader);

    auto* dash = new QLabel(QString::fromUtf8("起点 ———————— 终点"), page);
    dash->setAlignment(Qt::AlignCenter);
    dash->setStyleSheet("color:#9ab0c0;");
    headerRow->addWidget(dash);

    m_lblEndHeader = new QLabel(page);
    m_lblEndHeader->setTextFormat(Qt::RichText);
    m_lblEndHeader->setAlignment(Qt::AlignCenter);
    m_lblEndHeader->setStyleSheet(
        "border:1px solid #b9cfe0; border-radius:4px; padding:4px; background:#f4f8fb;");
    headerRow->addWidget(m_lblEndHeader);
    layout->addLayout(headerRow);

    // --- Left / right connection bubbles ---
    auto* bubbleRow = new QHBoxLayout();

    auto* startBox = new QGroupBox(QString::fromUtf8("起点侧连接"), page);
    auto* startBoxLayout = new QVBoxLayout(startBox);
    m_startBubble = new QWidget(startBox);
    m_startLayout = new QVBoxLayout(m_startBubble);
    m_startLayout->setContentsMargins(0, 0, 0, 0);
    m_startLayout->addStretch();
    startBoxLayout->addWidget(m_startBubble);
    bubbleRow->addWidget(startBox);

    auto* endBox = new QGroupBox(QString::fromUtf8("终点侧连接"), page);
    auto* endBoxLayout = new QVBoxLayout(endBox);
    m_endBubble = new QWidget(endBox);
    m_endLayout = new QVBoxLayout(m_endBubble);
    m_endLayout->setContentsMargins(0, 0, 0, 0);
    m_endLayout->addStretch();
    endBoxLayout->addWidget(m_endBubble);
    bubbleRow->addWidget(endBox);

    layout->addLayout(bubbleRow, 1);

    // --- Construction angle + detach ---
    auto* angleRow = new QHBoxLayout();
    angleRow->addWidget(new QLabel(QString::fromUtf8("构造角度:"), page));
    m_spinAngle = new QDoubleSpinBox(page);
    m_spinAngle->setRange(-360.0, 360.0);
    m_spinAngle->setDecimals(1);
    m_spinAngle->setSingleStep(5.0);
    m_spinAngle->setSuffix(QChar(0x00B0));
    m_spinAngle->setEnabled(false);
    angleRow->addWidget(m_spinAngle);
    angleRow->addStretch();
    layout->addLayout(angleRow);

    m_btnDetach = new QPushButton(QString::fromUtf8("解除附着"), page);
    m_btnDetach->setEnabled(false);
    layout->addWidget(m_btnDetach);

    auto* hint = new QLabel(
        QString::fromUtf8("· 单击连接卡片在画布选中对应线段，双击跳转编辑\n"
                          "· 构造角以前导线段方向为基准，逆时针为正"),
        page);
    hint->setStyleSheet("color:#888;");
    layout->addWidget(hint);

    tabs->addTab(page, QString::fromUtf8("相关"));
}

void LinePropertyDialog::populateFromModel()
{
    if (!m_paramDoc) return;

    cad::param::Block* block = m_paramDoc->findBlock(m_blockId);
    if (!block) return;

    cad::param::Segment* seg = block->findSegment(m_segmentId);
    if (!seg) return;

    m_lblSegId->setText(cad::param::Serial::toHtml(seg->serial));
    m_editName->setText(seg->name);
    m_chkShowName->setChecked(seg->showName);
    m_chkShowLength->setChecked(seg->showLength);

    // Length: show formula if present, otherwise show numeric in cm
    if (!seg->lengthFormula.isEmpty()) {
        m_editLength->setText(seg->lengthFormula);
    } else {
        const cad::param::ParamPoint* sp = block->findPoint(seg->startPointId);
        const cad::param::ParamPoint* ep = block->findPoint(seg->endPointId);
        if (sp && ep && sp->resolved && ep->resolved) {
            double lenMm = sp->resolvedPos.distanceTo(ep->resolvedPos);
            double lenCm = cad::geo::Units::mmToCm(lenMm);
            m_editLength->setText(QString::number(lenCm, 'f', 2));
        }
    }

    // Points
    if (auto* sp = block->findPoint(seg->startPointId)) {
        m_lblStartPtId->setText(cad::param::Serial::toHtml(sp->serial));
        m_editStartName->setText(sp->name);
        m_chkShowStartName->setChecked(sp->showName);
        m_editStartAnno->setText(sp->annotation);
    }
    if (auto* ep = block->findPoint(seg->endPointId)) {
        m_lblEndPtId->setText(cad::param::Serial::toHtml(ep->serial));
        m_editEndName->setText(ep->name);
        m_chkShowEndName->setChecked(ep->showName);
        m_editEndAnno->setText(ep->annotation);
    }

    m_cmbStyle->setCurrentIndex(static_cast<int>(seg->lineStyle));
    m_spinWeight->setValue(seg->weight);

    // Connections tab (endpoint bubbles) + follower angle/detach controls.
    refreshRelatedTab();

    // Save snapshot for cancel-revert
    m_snapshot.segName       = seg->name;
    m_snapshot.showName      = seg->showName;
    m_snapshot.showLength    = seg->showLength;
    m_snapshot.lengthFormula = seg->lengthFormula;
    if (auto* ep = block->findPoint(seg->endPointId))
        m_snapshot.distance = ep->distance;
    if (auto* sp = block->findPoint(seg->startPointId)) {
        m_snapshot.startName = sp->name;
        m_snapshot.startShowName = sp->showName;
        m_snapshot.startAnno = sp->annotation;
    }
    if (auto* ep = block->findPoint(seg->endPointId)) {
        m_snapshot.endName = ep->name;
        m_snapshot.endShowName = ep->showName;
        m_snapshot.endAnno = ep->annotation;
    }
    m_snapshot.lineStyle = static_cast<int>(seg->lineStyle);
    m_snapshot.weight    = seg->weight;
}

void LinePropertyDialog::applyToModel()
{
    if (!m_paramDoc) return;

    cad::param::Block* block = m_paramDoc->findBlock(m_blockId);
    if (!block) return;

    cad::param::Segment* seg = block->findSegment(m_segmentId);
    if (!seg) return;

    seg->name = m_editName->text().trimmed();
    seg->showName = m_chkShowName->isChecked();
    seg->showLength = m_chkShowLength->isChecked();

    // Length: parse as cm number or treat as formula (formula result is in cm)
    QString lenText = m_editLength->text().trimmed();
    bool isNumber = false;
    double numCm = lenText.toDouble(&isNumber);
    if (isNumber) {
        double numMm = cad::geo::Units::cmToMm(numCm);
        seg->lengthFormula.clear();
        if (auto* ep = block->findPoint(seg->endPointId)) {
            ep->distanceFormula.clear();
            ep->distance = numMm;
        }
    } else if (!lenText.isEmpty()) {
        // Store the raw cm formula; Block::resolve() evaluates it and
        // converts the result from cm to mm automatically.
        seg->lengthFormula = lenText;
        if (auto* ep = block->findPoint(seg->endPointId)) {
            ep->distanceFormula = lenText;
        }
    }

    // Point names/annotations
    if (auto* sp = block->findPoint(seg->startPointId)) {
        sp->name = m_editStartName->text().trimmed();
        sp->showName = m_chkShowStartName->isChecked();
        sp->annotation = m_editStartAnno->text().trimmed();
    }
    if (auto* ep = block->findPoint(seg->endPointId)) {
        ep->name = m_editEndName->text().trimmed();
        ep->showName = m_chkShowEndName->isChecked();
        ep->annotation = m_editEndAnno->text().trimmed();
    }

    seg->lineStyle = static_cast<cad::param::LineStyle>(m_cmbStyle->currentIndex());
    seg->weight = m_spinWeight->value();

    // Construction angle: only applies when this block is the follower.
    if (m_spinAngle->isEnabled()) {
        auto& attachments = const_cast<std::vector<cad::param::Attachment>&>(
            m_paramDoc->attachments());
        for (auto& att : attachments) {
            if (att.fromBlockId == m_blockId) {
                att.angleOffset = m_spinAngle->value();
                break;
            }
        }
    }
}

void LinePropertyDialog::refreshScene()
{
    if (m_paramDoc) m_paramDoc->resolveAll();
    if (m_scene) m_scene->refreshAllBlockItems();
}

void LinePropertyDialog::onLiveUpdate()
{
    applyToModel();
    refreshScene();
}

void LinePropertyDialog::onLengthTextChanged()
{
    // Enable and highlight the refresh button (orange)
    m_btnLenRefresh->setEnabled(true);
    m_btnLenRefresh->setStyleSheet(
        "QPushButton { background-color: #F5A623; color: white; font-weight: bold; }");
}

void LinePropertyDialog::onLengthRefresh()
{
    // Apply length to model and refresh preview
    applyToModel();
    refreshScene();

    // Reset button to normal state
    m_btnLenRefresh->setEnabled(false);
    m_btnLenRefresh->setStyleSheet(QString());
}

void LinePropertyDialog::onDetach()
{
    if (!m_paramDoc) return;

    // Find this block's follower attachment and remove it. The block keeps its
    // current transform (stays in place); only the constraint is released.
    QUuid attId;
    for (const auto& att : m_paramDoc->attachments()) {
        if (att.fromBlockId == m_blockId) {
            attId = att.id;
            break;
        }
    }
    if (attId.isNull()) return;

    m_paramDoc->removeAttachment(attId);

    // Switch UI back to the free-segment state and rebuild the connections tab.
    m_snapshot.followerAtt.reset();
    m_spinAngle->setEnabled(false);
    m_btnDetach->setEnabled(false);
    refreshRelatedTab();
    refreshScene();
}

void LinePropertyDialog::onAccepted()
{
    applyToModel();
    refreshScene();
    if (m_scene) m_scene->notifyGroupInfoChanged();
    m_confirmed = true;
    accept();
}

void LinePropertyDialog::onRejected()
{
    // Revert to snapshot
    if (m_paramDoc) {
        cad::param::Block* block = m_paramDoc->findBlock(m_blockId);
        if (block) {
            if (auto* seg = block->findSegment(m_segmentId)) {
                seg->name = m_snapshot.segName;
                seg->showName = m_snapshot.showName;
                seg->showLength = m_snapshot.showLength;
                seg->lengthFormula = m_snapshot.lengthFormula;
                seg->lineStyle = static_cast<cad::param::LineStyle>(m_snapshot.lineStyle);
                seg->weight = m_snapshot.weight;
            }
            if (auto* ep = block->findSegment(m_segmentId)
                    ? block->findPoint(block->findSegment(m_segmentId)->endPointId) : nullptr) {
                ep->distance = m_snapshot.distance;
                ep->name = m_snapshot.endName;
                ep->showName = m_snapshot.endShowName;
                ep->annotation = m_snapshot.endAnno;
            }
            if (auto* sp = block->findSegment(m_segmentId)
                    ? block->findPoint(block->findSegment(m_segmentId)->startPointId) : nullptr) {
                sp->name = m_snapshot.startName;
                sp->showName = m_snapshot.startShowName;
                sp->annotation = m_snapshot.startAnno;
            }
        }
        // Revert the follower attachment (angle change and/or detach).
        if (m_snapshot.followerAtt) {
            auto& attachments = const_cast<std::vector<cad::param::Attachment>&>(
                m_paramDoc->attachments());
            auto it = std::find_if(attachments.begin(), attachments.end(),
                [this](const cad::param::Attachment& a) {
                    return a.id == m_snapshot.followerAtt->id;
                });
            if (it != attachments.end()) {
                it->angleOffset = m_snapshot.followerAtt->angleOffset;
            } else {
                // Was detached during the dialog: re-add the original attachment.
                m_paramDoc->addAttachment(*m_snapshot.followerAtt);
            }
        }
        refreshScene();
    }

    if (m_scene) m_scene->notifyGroupInfoChanged();
    m_confirmed = false;
    reject();
}

void LinePropertyDialog::refreshRelatedTab()
{
    if (!m_paramDoc) return;
    cad::param::Block* block = m_paramDoc->findBlock(m_blockId);
    if (!block) return;
    cad::param::Segment* seg = block->findSegment(m_segmentId);
    if (!seg) return;

    const QUuid psId = seg->startPointId;
    const QUuid peId = seg->endPointId;
    const cad::param::ParamPoint* ps = block->findPoint(psId);
    const cad::param::ParamPoint* pe = block->findPoint(peId);

    // Endpoint headers (起点 / 终点 with readable serial + name).
    auto headerHtml = [](const QString& title, const cad::param::ParamPoint* p) {
        const QString serial = p ? p->serial : QString();
        const QString name = (p && !p->name.isEmpty()) ? p->name : QStringLiteral("—");
        return QStringLiteral("<b>%1</b> %2 %3")
            .arg(title, cad::param::Serial::toHtml(serial), name.toHtmlEscaped());
    };
    m_lblStartHeader->setText(headerHtml(QString::fromUtf8("起点"), ps));
    m_lblEndHeader->setText(headerHtml(QString::fromUtf8("终点"), pe));

    // Clear existing cards but keep the trailing stretch item.
    auto clearLayout = [](QVBoxLayout* lay) {
        while (lay->count() > 1) {
            QLayoutItem* it = lay->takeAt(0);
            if (it->widget()) it->widget()->deleteLater();
            delete it;
        }
    };
    clearLayout(m_startLayout);
    clearLayout(m_endLayout);

    // Gather connections per endpoint side.
    QList<ConnEntry> startEntries, endEntries;

    // This block as follower (被捕捉): its endpoint snaps to a leader segment.
    for (const auto& att : m_paramDoc->attachments()) {
        if (att.fromBlockId != m_blockId) continue;
        const bool startSide = (att.fromPointId == psId);
        const cad::param::Block* lb = m_paramDoc->findBlock(att.toBlockId);
        if (!lb || lb->segments.empty()) continue;
        const cad::param::Segment& lseg = lb->segments.front();
        const cad::param::ParamPoint* lp = lb->findPoint(att.toPointId);
        ConnEntry e;
        e.isLeader    = true;
        e.segSerial   = lseg.serial;
        e.segName     = lseg.name;
        e.pointSerial = lp ? lp->serial : QString();
        e.pointName   = lp ? lp->name : QString();
        e.angle       = att.angleOffset;
        e.blockId     = att.toBlockId;
        e.segmentId   = lseg.id;
        (startSide ? startEntries : endEntries).push_back(e);
    }

    // This block as leader (捕捉): follower segments snap to its endpoint.
    for (const auto& att : m_paramDoc->attachments()) {
        if (att.toBlockId != m_blockId) continue;
        const bool startSide = (att.toPointId == psId);
        const cad::param::Block* fb = m_paramDoc->findBlock(att.fromBlockId);
        if (!fb || fb->segments.empty()) continue;
        const cad::param::Segment& fseg = fb->segments.front();
        const cad::param::ParamPoint* fp = fb->findPoint(att.fromPointId);
        ConnEntry e;
        e.isLeader    = false;
        e.segSerial   = fseg.serial;
        e.segName     = fseg.name;
        e.pointSerial = fp ? fp->serial : QString();
        e.pointName   = fp ? fp->name : QString();
        e.angle       = att.angleOffset;
        e.blockId     = att.fromBlockId;
        e.segmentId   = fseg.id;
        (startSide ? startEntries : endEntries).push_back(e);
    }

    auto fill = [this](QVBoxLayout* lay, const QList<ConnEntry>& entries) {
        if (entries.isEmpty()) {
            auto* empty = new QLabel(QString::fromUtf8("（无连接）"), lay->widget());
            empty->setStyleSheet("color:#aaa; padding:4px;");
            empty->setAlignment(Qt::AlignCenter);
            lay->insertWidget(lay->count() - 1, empty);
            return;
        }
        for (const auto& e : entries) {
            auto* card = new ConnCard(e, m_scene, this, lay->widget());
            lay->insertWidget(lay->count() - 1, card);
        }
    };
    fill(m_startLayout, startEntries);
    fill(m_endLayout, endEntries);

    // Follower construction-angle + detach state.
    m_snapshot.followerAtt.reset();
    bool isFollower = false;
    for (const auto& att : m_paramDoc->attachments()) {
        if (att.fromBlockId != m_blockId) continue;
        isFollower = true;
        m_snapshot.followerAtt = att;
        m_spinAngle->setValue(att.angleOffset);
        break;
    }
    m_spinAngle->setEnabled(isFollower);
    m_btnDetach->setEnabled(isFollower);
}

void LinePropertyDialog::setTarget(const QUuid& blockId, const QUuid& segmentId)
{
    m_blockId = blockId;
    m_segmentId = segmentId;
    populateFromModel();

    if (m_paramDoc) {
        if (auto* b = m_paramDoc->findBlock(m_blockId)) {
            if (const auto* s = b->findSegment(m_segmentId)) {
                setWindowTitle(QString::fromUtf8("线段属性 - %1")
                    .arg(s->name.isEmpty()
                             ? cad::param::Serial::tag(s->serial)
                             : s->name));
            }
        }
    }
}

} // namespace cad::tools
