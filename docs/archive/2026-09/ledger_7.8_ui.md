#### 7.8.1 src/ui（29 条：已修 6 / 仍存在 23 / 刻意不改 0）

**已修（6）**
- **UI-P1-1** — U3 唯一实现 `src/parametric/ParamDocumentAttachments.cpp:43` `findFollowerAttachmentOf`；原 8 份自由函数全删，仅 `src/ui/SegmentAngleCard.cpp:285`、`src/ui/SegmentConnectionCard.cpp:50` 两处薄委托。
- **UI-P1-8** 已修（缩减） — N5 `src/ui/NumericFieldSpecs.h:27,30,33` 统一精度/后缀/步长（`kLengthCmSpec`/`kAngleDegSpec`/`kWeightPxSpec`）+ `:36 applyNumericSpec`；`setRange` 仍 7 处（`src/ui/LineAppearanceSection.cpp:117`、`src/ui/ConditionDialog.cpp:23-26`、`src/ui/SegmentAnchorTab.cpp:74,81,92,99`、`src/ui/VariableCard.cpp:98-100`）按 §7.5 刻意留调用点；`setValidator` 全 src/ui 仍 0 处（§7.5）。
- **UI-P1-9** — §7.6：清单内全部改 `parseAngleText`/`parseNumberOrFormula`，例 `src/ui/PlacedPointDialog.cpp:202,206`、`src/ui/SegmentShadowBasisCard.cpp:122`、`src/ui/ComponentTab.cpp:323,345`。
- **UI-P1-14** — 魔数 `/10.0`、`*10.0` 已清：`src/ui/PlacedPointDialog.cpp:189` `Units::formatCm(pt->interpOffsetDist)`、`:203` `cmToMm(dist.value)`（U4 + G4 禁 `/ 10.0`）。
- **UI-P2-7** — 原 4 处非 token 色全迁 token：`src/ui/PointRefEdit.cpp:276-278` `rgbaCss(tk.danger, 0.125)`、`src/ui/CardBase.cpp:215` `tokens().text3`；仅 `src/ui/Theme.cpp:351` 紫徽章在 `EXEMPT_FILES` 且刻意固定色相（`tools/check_hardcoded_colors.py:26`）。
- **UI-P2-12** — `src/ui/LineGeometrySection.cpp:318` 改 `Units::formatNumberTrimmed(seg.tension)`（U4 + G4 禁 `'f', N`）。

**仍存在（23）**
- **UI-P1-2** — 影子 Att1 循环仍 2 份：`src/ui/SegmentShadowBasisCard.cpp:78-80` 与 `:127-129` 逐字同构；同判据另见 `src/ui/LineEndpointSection.cpp:368-372`（共 4 处内联循环）。
- **UI-P1-3** — 两套连接语义仍在（约 150 行）：`src/ui/SegmentConnectionCardConn.cpp:60-86` vs `src/ui/LineEndpointSectionConn.cpp:37-60`；重定向 `:114-158` vs `:61-92`、建连 `:189-198` vs `:105-113`，无共享 helper。
- **UI-P1-4** — `src/ui/CardTabBase.cpp` 同套 QSS 仍写两遍：`:42-44/:111-114`、`:48-49/:116-118`、`:65-67/:120-122`、`:73-75/:125-127`、`:83-87/:130-134`。
- **UI-P1-5** — `src/ui/MeasureTab.h:25` 仍 `class MeasureTab : public QWidget`（其余三个走 CardTabBase）；`src/ui/MeasureTab.cpp:141,146` 自设 `cardListArea`/`cardListContainer`，空态 `:151` FontMd vs `src/ui/CardTabBase.cpp:62` FontXl。
- **UI-P1-6** — provider/sync 样板仍 4 份：`src/ui/VariableTab.cpp:29-53,117-129`、`src/ui/LinkedTab.cpp:44-83,106-116`、`src/ui/FormulaTab.cpp:71-…,430-436`、`src/ui/MeasureTab.cpp:172-249,263-298`；空态骨架仅 3/4 走 `setupListPage`。
- **UI-P1-7** — `src/ui/FormulaTab.cpp:393`、`:409` 有 dangling 检查，`:401` `if (!mv.refName.isEmpty())` 无；`mv.dangling` 字段见 `src/parametric/MeasureVariable.h:49`。
- **UI-P1-10** — `src/ui/PlacedPointDialog.cpp:79-96` 双框并存（"0.0"/"公式"、无互斥），`:202-208` 解析失败静默忽略；单框收口常量仍只在 `src/ui/ComponentTab.cpp:194,225` 等 5 处使用。
- **UI-P1-11** — `src/ui/AuxPointForm.cpp:55`「如 0.5 或公式」vs `:62`「如 0.7 (cm)或公式」；`src/ui/IntersectionForm.cpp:33`；`src/ui/LineEndpointSection.cpp:145` vs `src/ui/LinePropertyDialog.cpp:198` 名称提示不同。
- **UI-P1-12** — 两套 helper 并存：`makeDialogButtons`（`src/ui/ConditionDialog.cpp:147`、`src/ui/PlacedPointDialog.cpp:108` 等 4 处）vs `makeFormButtonBar`（`src/ui/MeasureResultDialog.cpp:118`、`src/ui/QuickAuxDialog.cpp:91`）。
- **UI-P1-13** — `src/ui/PlacedPointDialog.cpp:54,74` 仍手搭 `new QFormLayout()`，未调 `applyFormGrid`（`src/ui/AuxPointForm.cpp:105`、`src/ui/QuickAuxDialog.cpp:65` 等 5 处已用）。
- **UI-P1-15** — `src/ui/ComponentTab.cpp:152-165` 手搭 `componentCard` + `bar->setFixedWidth(3)` + 内联 QSS；`:176` `new QLabel` 自设 `componentIndex`；`:267-279` 四按钮未走 `makeFormButtonBar`。
- **UI-P2-1** — `src/ui/Theme.cpp:211-215` `QLabel#cardValue`/`[dangling="true"]` 仍在，全仓无 `setObjectName("cardValue")` → 死规则。
- **UI-P2-2** — 双来源仍在：`src/ui/Theme.cpp:216` `QLabel#cardIndex` + `src/ui/CardBase.cpp:144-148` `createIndexLabel` 内联 QSS。
- **UI-P2-3** — `src/ui/CardBase.cpp:142,168,181`（另 `:291`）仍 `new ElaText(QString(), 13, this)`，随后被 QSS 覆盖为 FontXs/FontLg。
- **UI-P2-4** — `src/ui/Theme.cpp:354-359` `dimValueStyle` 仍硬编码 11px 且无等宽族；`src/ui/LineOrthoOffsetCard.cpp:38` 整串自拼，`src/ui/LinePropertyDialog.cpp:184-185` 自行追加等宽族。
- **UI-P2-5** — `src/ui` 内 `font-size:\s*\d+px` **93 处**（带空格 63 / 无空格 30；审计记约 68 处），例 `src/ui/CardTabBase.cpp:74,85,126,132`、`src/ui/LayerPanel.cpp:104,116,179,239`、`src/ui/LayerCard.cpp:97,111`；§7.4 登记未做。
- **UI-P2-6** — 非 token 圆角仍在：3px `src/ui/LayerPanel.cpp:104,115,203,209`、`src/ui/LayerCard.cpp:78,244,259,433`、`src/ui/FormulaCard.cpp:355`；8px `src/ui/NoteButton.cpp:125`；15px `src/ui/PointRefEdit.cpp:60,276`；1px `src/ui/ComponentTab.cpp:163`。
- **UI-P2-8** — `constexpr int kFieldH = 30` 仍 8 处（`src/ui/SegmentAngleCard.cpp:32`、`src/ui/LineGeometrySection.cpp:42`、`src/ui/LinePropertyDialog.cpp:44` 等；src/ui 内共 9 份本地定义）；`src/ui/LineEndpointSection.cpp:37` 仍 26；高度另有 34/32/28/24/22/20/18。
- **UI-P2-9** — `src/ui/CardBase.h:115` `int refChipWidth = 72`；`src/ui/LinkedCard.cpp:91`=72、`src/ui/MeasureCard.cpp:140`=72、`src/ui/AngleMeasureCard.cpp:116`=84 各写死。
- **UI-P2-10** — `src/ui/MeasureCard.cpp:71-82` 仍把「水平/垂直 」塞进值标签，`:141` 又有独立 `spec.unit="cm"`；`src/ui/SegmentAnchorTab.cpp:73` 标签「(°)」+ `:77` 后缀 "°" 双单位。
- **UI-P2-11** — `src/ui/LineGeometrySection.cpp:183,196,252` 仍 `setPlaceholderText("0")`；`src/ui/LineEndpointSection.cpp:182`、`src/ui/SegmentConnectionCardBuild.cpp:176` 同，均不带单位。
- **UI-P2-13** — `src/ui/SegmentAnchorTab.cpp:5` 与 `:6` 仍重复 `#include "ElaTabWidget.h"`。
- **UI-P2-14** — `'g',6` 仍 3 文件 6 处：`src/ui/IntersectionForm.cpp:89,107`、`src/ui/AuxPointForm.cpp:178,184`、`src/ui/QuickAuxDialog.cpp:81`、`src/ui/SegmentAngleCard.cpp:476`；与 `formatNumberTrimmed`/`formatDegValue` 并存（`src/ui/PlacedPointDialog.cpp` 的 `'f'` 已改）。

**刻意不改（0）** — 无新增；`setRange` 留调用点与不装 `QDoubleValidator` 已在 §7.5 登记（对应 UI-P1-8 的缩减结论）。
