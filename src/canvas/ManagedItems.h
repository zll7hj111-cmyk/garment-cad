#pragma once

#include <QGraphicsItem>

#include <functional>
#include <vector>

/// RAII 登记式图元容器（TOOL_SYSTEM_AUDIT P1/L1，2026-08-29 收口）。
///
/// 工具/手势把临时图元（预览线、标记、高亮环…）登记进来，容器析构时统一
/// 脱离 scene 并 delete——消灭散落各 deactivate 里的
/// `if (m_x) { m_scene->removeItem(m_x); delete m_x; m_x = nullptr; }`
/// 手写清理样板（漏一处即泄漏，M2 的重叠提示曾是先例；ToolCurveEdit 的
/// 清理块甚至不置空指针，属悬空隐患）。
///
/// 契约：
///   · 每个图元只在**创建点** own() 一次（重建 = 先 release 再重新 own），
///     容器不查重，重复 own 同一指针 = 析构双重释放；
///   · shadow 绑定工具的原始指针成员，clear/release 时自动置空——杜绝
///     "释放后成员悬空"；
///   · QGraphicsItem 析构自行脱离 scene，无需持有 scene 指针。
/// 放在 canvas 层（与 HudItem 同理：管理的是场景图元，工具层经 include 使用）。
class ManagedItems
{
public:
    ManagedItems() = default;
    ~ManagedItems() { clear(); }
    ManagedItems(const ManagedItems&) = delete;
    ManagedItems& operator=(const ManagedItems&) = delete;

    /// 登记并接管图元。@p shadow 可选绑定原始指针成员（类型安全：传
    /// `&m_previewLine` 这样的 T** 即可），clear/release 时自动置空。
    template <class T>
    void own(T* item, T** shadow = nullptr)
    {
        Slot s;
        s.item = item;
        if (shadow) s.reset = [shadow] { *shadow = nullptr; };
        m_slots.push_back(std::move(s));
    }

    /// 提前释放单个登记图元并置空其影子（会话中途"释放-重建"型图元用，
    /// 如旋转工具的瞄准环）。幂等：未登记项无操作。
    void release(QGraphicsItem* item)
    {
        for (auto it = m_slots.begin(); it != m_slots.end(); ++it) {
            if (it->item == item) {
                if (it->reset) it->reset();
                delete item;   // 析构自行脱离 scene
                m_slots.erase(it);
                return;
            }
        }
    }

    /// 释放全部登记图元并置空影子指针。幂等。
    void clear()
    {
        for (Slot& s : m_slots) {
            if (s.reset) s.reset();
            delete s.item;
        }
        m_slots.clear();
    }

    /// 隐藏全部（保留对象，供每帧复用的预览图元切显隐）。
    void hideAll()
    {
        for (Slot& s : m_slots) s.item->hide();
    }

private:
    struct Slot {
        QGraphicsItem* item = nullptr;
        std::function<void()> reset;   ///< 置空影子指针（未绑定为空）。
    };
    std::vector<Slot> m_slots;
};
