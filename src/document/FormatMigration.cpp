#include "FormatMigration.h"

#include <QJsonArray>
#include <QJsonValue>
#include <QUuid>

#include <algorithm>

namespace cad::doc {
namespace {

/// Function-local registry: built on first use, so registration order across
/// translation units can never bite (static initialisation order fiasco).
std::vector<MigrationStep>& registry()
{
    static std::vector<MigrationStep> steps = {
        // ── append new links here; `from` is the version UPGRADED FROM ──
        {0, "layer-refs", &FormatMigration::migrateV0ToV1},
        {1, "shadow-anchor", &FormatMigration::migrateV1ToV2},
    };
    return steps;
}

QString uuidStr(const QUuid& id) { return id.toString(QUuid::WithoutBraces); }

/// The layer pair a v0 document implicitly had: one working layer only. The
/// auxiliary layer of v1 is inserted in front of it, which is what makes every
/// v0 integer index shift up by one.
QJsonArray v0DefaultLayers()
{
    QJsonArray layers;
    QJsonObject working;
    working["id"]       = uuidStr(QUuid::createUuid());
    working["name"]     = QStringLiteral("图层 1");
    working["visible"]  = true;
    working["type"]     = QStringLiteral("working");
    layers.append(working);
    return layers;
}

QJsonObject auxLayerObject(const QString& name = QStringLiteral("辅助层"))
{
    QJsonObject aux;
    aux["id"]      = uuidStr(QUuid::createUuid());
    aux["name"]    = name;
    aux["visible"] = true;
    aux["type"]    = QStringLiteral("auxiliary");
    return aux;
}

bool isAuxiliary(const QJsonObject& layer)
{
    return layer["type"].toString() == QLatin1String("auxiliary");
}

} // namespace

void FormatMigration::registerStep(const MigrationStep& step)
{
    registry().push_back(step);
}

std::vector<MigrationStep> FormatMigration::steps()
{
    std::vector<MigrationStep> out = registry();
    std::sort(out.begin(), out.end(),
              [](const MigrationStep& a, const MigrationStep& b) {
                  return a.from < b.from;
              });
    return out;
}

bool FormatMigration::migrate(int fromVersion, QJsonObject& root,
                              QStringList* warnings, QString* error)
{
    if (fromVersion == kFormatVersion)
        return true;

    if (fromVersion > kFormatVersion) {
        if (error)
            *error = QStringLiteral("文件格式版本 %1 不受支持（当前支持 v%2）")
                         .arg(fromVersion).arg(kFormatVersion);
        return false;
    }

    for (int v = fromVersion; v < kFormatVersion; ++v) {
        const MigrationStep* step = nullptr;
        for (const auto& s : registry()) {
            if (s.from == v) { step = &s; break; }
        }
        if (!step || !step->fn) {
            if (error)
                *error = QStringLiteral("缺少 v%1 → v%2 的格式迁移步骤，无法安全读取该文件")
                             .arg(v).arg(v + 1);
            return false;
        }
        root = step->fn(std::move(root), warnings);
    }
    return true;
}

QJsonObject FormatMigration::migrateV0ToV1(QJsonObject root, QStringList* warnings)
{
    QJsonObject docObj = root["document"].toObject();

    // ── 1. Establish the layer array ───────────────────────────────────────
    // v0 had no auxiliary layer and no per-layer ids; a file without a
    // "layers" array means exactly that (one implicit working layer).
    QJsonArray layers = docObj["layers"].toArray();
    if (layers.isEmpty())
        layers = v0DefaultLayers();

    // Give every layer a stable id (v0 files never had one).
    for (int i = 0; i < layers.size(); ++i) {
        QJsonObject l = layers[i].toObject();
        if (l["id"].toString().isEmpty())
            l["id"] = uuidStr(QUuid::createUuid());
        layers[i] = l;
    }

    // ── 2. Insert the auxiliary layer (shifts every v0 index up by one) ────
    bool hasAux = false;
    for (int i = 0; i < layers.size(); ++i) {
        if (isAuxiliary(layers[i].toObject())) { hasAux = true; break; }
    }
    const int shift = hasAux ? 0 : 1;
    if (!hasAux) {
        QJsonArray withAux;
        withAux.append(auxLayerObject());
        for (int i = 0; i < layers.size(); ++i)
            withAux.append(layers[i]);
        layers = withAux;
    }

    const int layerCount = layers.size();
    auto idAt = [&layers, layerCount](int index) {
        const int clamped = std::clamp(index, 0, layerCount - 1);
        return layers[clamped].toObject()["id"].toString();
    };

    // ── 3. Blocks: integer layer index → stable id string ──────────────────
    // A block with NO "layer" key at all is left alone: the serializer's
    // "missing layer field" defaulting (first working layer) still applies and
    // must keep applying — inventing a 0 here would silently move it.
    QJsonArray blocks = docObj["blocks"].toArray();
    int remapped = 0;
    for (int i = 0; i < blocks.size(); ++i) {
        QJsonObject b = blocks[i].toObject();
        if (!b.contains(QStringLiteral("layer")))
            continue;
        const QJsonValue layerVal = b["layer"];
        if (layerVal.isString())
            continue;                       // already v1-shaped
        b["layer"] = idAt(layerVal.toInt(0) + shift);
        blocks[i] = b;
        ++remapped;
    }
    docObj["blocks"] = blocks;

    // ── 4. Active layer: integer index → stable id ─────────────────────────
    // The historical reader used `toInt(1) + shift` (default = first working
    // layer), so keep that exact expression.
    const QJsonValue activeVal = docObj["activeLayer"];
    if (!activeVal.isString())
        docObj["activeLayer"] = idAt(activeVal.toInt(1) + shift);
    // A string value is already v1-shaped and is left alone — including the
    // empty string, whose meaning ("no active layer") the serializer owns.

    docObj["layers"] = layers;
    root["document"] = docObj;

    if (warnings && (remapped > 0 || !hasAux)) {
        warnings->append(
            QStringLiteral("已将旧版文件（v0 → v1）迁移为稳定图层 id："
                           "插入辅助层并重写 %1 处图层索引引用")
                .arg(remapped));
    }
    return root;
}

QJsonObject FormatMigration::migrateV1ToV2(QJsonObject root, QStringList* warnings)
{
    QJsonObject docObj = root["document"].toObject();

    // ── v11 影子偏移账本 (baselineOffsetDeg, 度) 已随影子偏转功能删除 ─────
    // 此步只清理残留键: 影子锚点换算 (锚 = 宿主旋转 − 旧偏移) 与读取端
    // 锚点字段均已随功能删除, 旧偏移对当前语义无任何影响, 直接丢弃。
    QJsonArray atts = docObj["attachments"].toArray();
    int removed = 0;
    for (int i = 0; i < atts.size(); ++i) {
        QJsonObject a = atts[i].toObject();
        if (a.contains(QStringLiteral("baselineOffsetDeg"))) {
            a.remove(QStringLiteral("baselineOffsetDeg"));
            atts[i] = a;
            ++removed;
        }
    }
    if (removed > 0)
        docObj["attachments"] = atts;
    root["document"] = docObj;

    if (warnings && removed > 0) {
        warnings->append(
            QStringLiteral("已清理旧版文件（v1 → v2）的影子偏移残留字段："
                           "丢弃 %1 处连接的 baselineOffsetDeg")
                .arg(removed));
    }
    return root;
}

} // namespace cad::doc
