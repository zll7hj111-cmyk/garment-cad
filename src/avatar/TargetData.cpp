#include "TargetData.h"

#include <miniz.h>

#include <charconv>
#include <cstring>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace cad::avatar {

namespace {

constexpr uint32_t kBinMagic = 0x54414347; ///< 'GCAT' little-endian
constexpr uint32_t kBinVersion = 1;

/// 跳过 .target 文件中的 # 注释行，返回下一条数据行（可能为 empty）。
std::string nextDataLine(std::istream& in) {
    std::string line;
    while (std::getline(in, line)) {
        // 去除行尾 \r（Windows 换行）
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        return line;
    }
    return {};
}

/// 读取整个文件到内存。失败抛异常。
std::vector<uint8_t> readWholeFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("loadTargetBin: cannot open file: " + path);
    in.seekg(0, std::ios::end);
    const std::streamoff size = in.tellg();
    if (size < 0) throw std::runtime_error("loadTargetBin: tellg failed: " + path);
    in.seekg(0, std::ios::beg);
    std::vector<uint8_t> data(static_cast<size_t>(size));
    if (!data.empty()) in.read(reinterpret_cast<char*>(data.data()), size);
    if (!in) throw std::runtime_error("loadTargetBin: read failed: " + path);
    return data;
}

/// GCAT 二进制目标文件：'GCAT'(4B) | u32 version | u32 rawSize | zlib( u32 count + [u32 idx, f32 x, f32 y, f32 z]*count )
TargetData parseBin(const std::vector<uint8_t>& data, const std::string& path) {
    if (data.size() < 12)
        throw std::runtime_error("loadTargetBin: truncated header: " + path);
    uint32_t magic = 0, version = 0, rawSize32 = 0;
    std::memcpy(&magic, data.data(), 4);
    std::memcpy(&version, data.data() + 4, 4);
    std::memcpy(&rawSize32, data.data() + 8, 4);
    if (magic != kBinMagic)
        throw std::runtime_error("loadTargetBin: bad magic in: " + path);
    if (version != kBinVersion)
        throw std::runtime_error("loadTargetBin: unsupported version " + std::to_string(version) + " in: " + path);

    mz_ulong rawSize = rawSize32;
    std::vector<uint8_t> raw(rawSize);
    const int rc = mz_uncompress(raw.data(), &rawSize, data.data() + 12, data.size() - 12);
    if (rc != MZ_OK)
        throw std::runtime_error("loadTargetBin: inflate failed (" + std::to_string(rc) + ") in: " + path);

    if (rawSize < 4)
        throw std::runtime_error("loadTargetBin: truncated payload in: " + path);
    uint32_t count = 0;
    std::memcpy(&count, raw.data(), 4);
    const size_t expected = 4 + static_cast<size_t>(count) * 16;
    if (rawSize != expected)
        throw std::runtime_error("loadTargetBin: size mismatch in: " + path);

    TargetData td;
    td.indices.reserve(count);
    td.deltas.reserve(count);
    const uint8_t* p = raw.data() + 4;
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t idx;
        float dx, dy, dz;
        std::memcpy(&idx, p, 4);
        std::memcpy(&dx, p + 4, 4);
        std::memcpy(&dy, p + 8, 4);
        std::memcpy(&dz, p + 12, 4);
        td.indices.push_back(static_cast<int>(idx));
        td.deltas.emplace_back(dx, dy, dz);
        p += 16;
    }
    return td;
}

} // namespace

TargetData loadTargetFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("loadTargetFile: cannot open file: " + path);

    TargetData td;
    for (std::string line = nextDataLine(in); !line.empty(); line = nextDataLine(in)) {
        std::istringstream ss(line);
        int idx;
        double dx, dy, dz;
        if (!(ss >> idx >> dx >> dy >> dz))
            throw std::runtime_error("loadTargetFile: malformed data line in: " + path);
        if (idx < 0)
            throw std::runtime_error("loadTargetFile: negative vertex index in: " + path);
        td.indices.push_back(idx);
        td.deltas.emplace_back(dx, dy, dz);
    }
    return td;
}

void TargetCache::setRootDir(std::string root) {
    m_root = std::move(root);
    m_cache.clear();
}

void TargetCache::setVertexRemap(std::vector<int> remap) {
    m_vertexRemap = std::move(remap);
    m_cache.clear();
}


const TargetData& TargetCache::get(const std::string& key) {
    if (m_root.empty())
        throw std::runtime_error("TargetCache: root dir not set");
    auto it = m_cache.find(key);
    if (it != m_cache.end()) return it->second;

    // 优先二进制格式（.bin，zlib 压缩），缺失时回退 .target 文本
    TargetData td;
    try {
        td = parseBin(readWholeFile(m_root + "/" + key + ".bin"), key);
    } catch (const std::runtime_error&) {
        td = loadTargetFile(m_root + "/" + key + ".target");
    }

    // 顶点重映射：过滤被排除顶点、重映射保留顶点索引到缩减网格。
    if (!m_vertexRemap.empty()) {
        TargetData mapped;
        mapped.indices.reserve(td.indices.size());
        mapped.deltas.reserve(td.deltas.size());
        for (size_t i = 0; i < td.indices.size(); ++i) {
            const int oldIdx = td.indices[i];
            if (oldIdx < 0 || oldIdx >= static_cast<int>(m_vertexRemap.size()))
                continue; // 越界索引（坏数据）丢弃
            const int newIdx = m_vertexRemap[static_cast<size_t>(oldIdx)];
            if (newIdx < 0)
                continue; // 顶点被排除
            mapped.indices.push_back(newIdx);
            mapped.deltas.push_back(td.deltas[i]);
        }
        td = std::move(mapped);
    }

    return m_cache.emplace(key, std::move(td)).first->second;
}

const TargetData* TargetCache::findCached(const std::string& key) const {
    auto it = m_cache.find(key);
    return it != m_cache.end() ? &it->second : nullptr;
}

} // namespace cad::avatar
