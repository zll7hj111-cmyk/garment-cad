#include "FormatMigration.h"

#include <QHash>
#include <QJsonArray>
#include <QJsonValue>
#include <QUuid>

#include <algorithm>
#include <cmath>

namespace cad::doc {
namespace {

/// 度 → 弧度 (局部常量, 不依赖 M_PI 的传递定义)。
constexpr double kDegToRad = 3.14159265358979323846 / 180.0;

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

    // ── 影子账本: 累计偏移 (v11 baselineOffsetDeg, 度) → 锚点 (v14
    // shadowAnchorRotDeg, 弧度) ────────────────────────────────────────────
    // 旧语义: 有效基准 = 真基准 + offset (旋转会话逐帧回写 base+δ)。
    // 新语义: 有效基准 = 真基准 + (宿主旋转 − 锚点), 每次求解现算。
    // 换算: 锚 = 宿主旋转 − 旧偏移 (度 → 弧度)。宿主块缺失时锚 = 0
    // (序列化器的悬空连接处理拥有该场景)。
    QJsonArray blocks = docObj["blocks"].toArray();
    QHash<QString, double> blockRot;
    for (const auto& v : blocks) {
        const QJsonObject b = v.toObject();
        blockRot.insert(b["id"].toString(), b["rotation"].toDouble());
    }

    QJsonArray atts = docObj["attachments"].toArray();
    int converted = 0;
    for (int i = 0; i < atts.size(); ++i) {
        QJsonObject a = atts[i].toObject();
        // 旧偏移缺省 0 (旧行为 = 无影子偏转); 锚 = 宿主旋转 − 旧偏移。
        // 无 baselineOffsetDeg 字段的连接同样要写锚点 —— 否则新读取端
        // 锚点缺省 0 会把宿主旋转暴露成意外偏转。
        const double oldOffsetDeg = a["baselineOffsetDeg"].toDouble(0.0);
        const QString hostId = a["toBlockId"].toString();
        const double hostRot = blockRot.value(hostId, 0.0);
        const double anchor = hostRot - oldOffsetDeg * kDegToRad;
        a["shadowAnchorRotDeg"] = anchor;
        a.remove(QStringLiteral("baselineOffsetDeg"));
        atts[i] = a;
        ++converted;
    }
    docObj["attachments"] = atts;
    root["document"] = docObj;

    if (warnings && converted > 0) {
        warnings->append(
            QStringLiteral("已将旧版文件（v1 → v2）的影子偏转账本迁移为锚点："
                           "重写 %1 处连接")
                .arg(converted));
    }
    return root;
}

} // namespace cad::doc
