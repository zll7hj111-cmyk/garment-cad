#include "ParamDocument.h"

#include <algorithm>

#include "parametric/Resolver.h"
#include "parametric/Serial.h"
#include "parametric/GroupModel.h"

namespace cad::param {

ParamDocument::ParamDocument(QObject* parent)
    : QObject(parent)
    , m_undoStack(new QUndoStack(this))
{
}

ParamDocument::~ParamDocument() = default;

// --- Parameters ---

void ParamDocument::setParameter(const QString& name, double value)
{
    m_parameters[name] = value;
    resolveAll();
}

void ParamDocument::setParameters(const QHash<QString, double>& nameValues)
{
    for (auto it = nameValues.cbegin(); it != nameValues.cend(); ++it)
        m_parameters[it.key()] = it.value();
    resolveAll();
}

void ParamDocument::removeParameter(const QString& name)
{
    if (m_parameters.remove(name))
        resolveAll();
}

void ParamDocument::syncFormulaParameters(const QHash<QString, double>& cmValues)
{
    // Remove stale formula-derived parameters from the previous sync.
    for (const QString& old : std::as_const(m_formulaParamNames)) {
        if (!cmValues.contains(old))
            m_parameters.remove(old);
    }
    m_formulaParamNames = QSet<QString>(cmValues.keyBegin(), cmValues.keyEnd());

    // Insert / update current formula parameters (cm).
    for (auto it = cmValues.cbegin(); it != cmValues.cend(); ++it)
        m_parameters[it.key()] = it.value();

    resolveAll();
}

void ParamDocument::syncFormulaConditions(const QHash<QString, QList<Condition>>& conditioned)
{
    m_conditioned = conditioned;
}

double ParamDocument::parameter(const QString& name, double defaultVal) const
{
    return m_parameters.value(name, defaultVal);
}

// --- Free points ---

void ParamDocument::addFreePoint(ParamPoint pt)
{
    m_freePoints.push_back(std::move(pt));
    emit documentChanged();
}

void ParamDocument::removeFreePoint(const QUuid& id)
{
    auto it = std::find_if(m_freePoints.begin(), m_freePoints.end(),
        [&id](const ParamPoint& p) { return p.id == id; });
    if (it != m_freePoints.end()) {
        m_freePoints.erase(it);
        emit documentChanged();
    }
}

ParamPoint* ParamDocument::findFreePoint(const QUuid& id)
{
    auto it = std::find_if(m_freePoints.begin(), m_freePoints.end(),
        [&id](const ParamPoint& p) { return p.id == id; });
    return (it != m_freePoints.end()) ? &(*it) : nullptr;
}

// --- Blocks ---

QUuid ParamDocument::addBlock(Block block)
{
    QUuid id = block.id;
    // Assign readable serials to any points/segments that lack one.
    for (auto& pt : block.points)
        if (pt.serial.isEmpty()) pt.serial = newPointSerial();
    for (auto& seg : block.segments)
        if (seg.serial.isEmpty()) seg.serial = newLineSerial();
    block.resolve(m_parameters, m_conditioned);
    m_blockIndex.insert(id, static_cast<int>(m_blocks.size()));
    m_blocks.push_back(std::move(block));
    emit blockAdded(id);
    emit documentChanged();
    recomputeGroups();
    emit structureChanged();
    return id;
}

void ParamDocument::removeBlock(const QUuid& id)
{
    auto it = m_blockIndex.find(id);
    if (it == m_blockIndex.end()) return;

    const int idx = it.value();
    m_blocks.erase(m_blocks.begin() + idx);
    m_blockIndex.erase(it);

    // Rebuild index for elements after the removed one.
    for (int i = idx; i < static_cast<int>(m_blocks.size()); ++i)
        m_blockIndex[m_blocks[i].id] = i;

    // Also remove attachments referencing this block
    m_attachments.erase(
        std::remove_if(m_attachments.begin(), m_attachments.end(),
            [&id](const Attachment& a) {
                return a.fromBlockId == id || a.toBlockId == id;
            }),
        m_attachments.end());

    emit blockRemoved(id);
    emit documentChanged();
    recomputeGroups();
    emit structureChanged();
}

Block* ParamDocument::findBlock(const QUuid& id)
{
    return blockById(id);
}

const Block* ParamDocument::findBlock(const QUuid& id) const
{
    return blockById(id);
}

Block* ParamDocument::blockById(const QUuid& id)
{
    auto it = m_blockIndex.find(id);
    if (it == m_blockIndex.end()) return nullptr;
    return &m_blocks[it.value()];
}

const Block* ParamDocument::blockById(const QUuid& id) const
{
    auto it = m_blockIndex.find(id);
    if (it == m_blockIndex.end()) return nullptr;
    return &m_blocks[it.value()];
}

// --- Attachments ---

void ParamDocument::addAttachment(Attachment att)
{
    m_attachments.push_back(std::move(att));
    resolveAll();
    recomputeGroups();
    emit structureChanged();
}

void ParamDocument::removeAttachment(const QUuid& id)
{
    auto it = std::find_if(m_attachments.begin(), m_attachments.end(),
        [&id](const Attachment& a) { return a.id == id; });
    if (it != m_attachments.end()) {
        m_attachments.erase(it);
        resolveAll();
        recomputeGroups();
        emit structureChanged();
    }
}

int ParamDocument::removeAttachmentsOfBlock(const QUuid& blockId)
{
    const auto before = m_attachments.size();
    m_attachments.erase(
        std::remove_if(m_attachments.begin(), m_attachments.end(),
            [&blockId](const Attachment& a) {
                return a.fromBlockId == blockId || a.toBlockId == blockId;
            }),
        m_attachments.end());
    const int removed = static_cast<int>(before - m_attachments.size());
    if (removed > 0) {
        resolveAll();
        recomputeGroups();
        emit structureChanged();
    }
    return removed;
}

// --- Readable serials ---

QString ParamDocument::newPointSerial()
{
    return Serial::make(Serial::randomPrefix(), QLatin1Char('P'), m_nextPointSeq++);
}

QString ParamDocument::newLineSerial()
{
    return Serial::make(Serial::randomPrefix(), QLatin1Char('L'), m_nextLineSeq++);
}

QString ParamDocument::newGroupSerial()
{
    return Serial::make(Serial::randomPrefix(), QLatin1Char('G'), m_nextGroupSeq++);
}

// --- Groups ---

void ParamDocument::recomputeGroups()
{
    // Use shared GroupModel helper to find connected components (>= 2 blocks).
    const auto components = findAllComponents(*this);

    // Reconcile with the previous mapping to preserve group identity/name.
    const QHash<QUuid, QUuid> oldBlockGroup = m_blockGroup;

    QHash<QUuid, Group> oldGroupsById;
    for (const auto& g : m_groups)
        oldGroupsById.insert(g.id, g);

    QHash<QUuid, QUuid> newBlockGroup;
    QSet<QUuid> claimedOldGroups;
    std::vector<Group> freshGroups;

    for (const auto& comp : components) {
        // Tally old group ids among members to find the dominant one.
        QHash<QUuid, int> tally;
        for (const QUuid& blk : comp) {
            const QUuid og = oldBlockGroup.value(blk);
            if (!og.isNull())
                tally[og]++;
        }
        QUuid candidate;
        int best = 0;
        for (const QUuid& blk : comp) {
            const QUuid og = oldBlockGroup.value(blk);
            if (og.isNull()) continue;
            if (tally.value(og) > best) {
                best = tally.value(og);
                candidate = og;
            }
        }

        QUuid gid;
        if (!candidate.isNull() && !claimedOldGroups.contains(candidate)
            && oldGroupsById.contains(candidate)) {
            gid = candidate;
            claimedOldGroups.insert(candidate);
        } else {
            Group g;
            g.serial = newGroupSerial();
            gid = g.id;
            freshGroups.push_back(std::move(g));
        }
        for (const QUuid& blk : comp)
            newBlockGroup.insert(blk, gid);
    }

    // Rebuild the registry.
    std::vector<Group> merged;
    merged.reserve(freshGroups.size() + claimedOldGroups.size());
    for (auto& g : freshGroups)
        merged.push_back(std::move(g));
    for (const QUuid& gid : claimedOldGroups)
        merged.push_back(oldGroupsById.value(gid));

    m_groups = std::move(merged);
    m_blockGroup = std::move(newBlockGroup);

    emit groupsChanged();
}

Group* ParamDocument::findGroup(const QUuid& groupId)
{
    auto it = std::find_if(m_groups.begin(), m_groups.end(),
        [&groupId](const Group& g) { return g.id == groupId; });
    return (it != m_groups.end()) ? &(*it) : nullptr;
}

QUuid ParamDocument::groupOfBlock(const QUuid& blockId) const
{
    return m_blockGroup.value(blockId);
}

QList<QUuid> ParamDocument::blocksInGroup(const QUuid& groupId) const
{
    QList<QUuid> result;
    for (auto it = m_blockGroup.cbegin(); it != m_blockGroup.cend(); ++it) {
        if (it.value() == groupId)
            result.push_back(it.key());
    }
    return result;
}

void ParamDocument::setGroupName(const QUuid& groupId, const QString& name)
{
    if (Group* g = findGroup(groupId)) {
        if (g->name != name) {
            g->name = name;
            emit groupsChanged();
        }
    }
}

// --- Resolve ---

void ParamDocument::resolveAll()
{
    Resolver::resolveAll(m_blocks, m_attachments, m_parameters, m_conditioned);
    emit resolved();
    emit documentChanged();
}

} // namespace cad::param
