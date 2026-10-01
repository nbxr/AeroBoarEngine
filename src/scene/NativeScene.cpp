#include "scene/NativeScene.h"

#include "core/Log.h"

#include <zstd.h>

#include <bit>
#include <cctype>
#include <chrono>
#include <cstring>
#include <fstream>
#include <string>
#include <utility>

namespace scene {
namespace {

static_assert(std::endian::native == std::endian::little, "abn is little-endian");
static_assert(sizeof(gfx::Material) == 256, "native MATL is a 256-byte memcpy");
static_assert(sizeof(gfx::Vertex) == 64, "native MESH vertices are 64-byte memcpy");
static_assert(sizeof(gfx::MeshletDesc) == 64, "native MESH meshlets are 64-byte memcpy");

constexpr uint32_t kMagic = 0x314E4241u; // 'ABN1'
constexpr uint32_t kMaxCount = (1u << 20) - 1u;
constexpr uint32_t kMaxImages = 4096u;
constexpr uint32_t kMaxMeshes = 100000u;
constexpr uint32_t kMaxVerts = 5000000u;
constexpr uint32_t kMaxIndices = 15000000u;
constexpr uint32_t kMaxString = (1u << 20) - 1u;
constexpr uint32_t kMaxName = 1024u;
constexpr uint32_t kMaxDim = 16384u;
constexpr uint64_t kMaxFile = 4ull << 30;
constexpr uint32_t kMaxShape = 32u << 20;
// zstd level 3 with a content checksum. On ABeautifulGameGame this is
// about 4x smaller than the raw document.
constexpr int kZstdLevel = 3;

constexpr uint32_t fourcc(char a, char b, char c, char d) {
    return static_cast<uint32_t>(static_cast<uint8_t>(a)) |
           (static_cast<uint32_t>(static_cast<uint8_t>(b)) << 8) |
           (static_cast<uint32_t>(static_cast<uint8_t>(c)) << 16) |
           (static_cast<uint32_t>(static_cast<uint8_t>(d)) << 24);
}

constexpr uint32_t kText = fourcc('T', 'E', 'X', 'T');
constexpr uint32_t kMatl = fourcc('M', 'A', 'T', 'L');
constexpr uint32_t kMesh = fourcc('M', 'E', 'S', 'H');
constexpr uint32_t kXfrm = fourcc('X', 'F', 'R', 'M');
constexpr uint32_t kNode = fourcc('N', 'O', 'D', 'E');
constexpr uint32_t kGobj = fourcc('G', 'O', 'B', 'J');
constexpr uint32_t kRend = fourcc('R', 'E', 'N', 'D');
constexpr uint32_t kSkin = fourcc('S', 'K', 'I', 'N');
constexpr uint32_t kAnim = fourcc('A', 'N', 'I', 'M');
constexpr uint32_t kMrph = fourcc('M', 'R', 'P', 'H');
constexpr uint32_t kLght = fourcc('L', 'G', 'H', 'T');
constexpr uint32_t kCamr = fourcc('C', 'A', 'M', 'R');
constexpr uint32_t kEcs1 = fourcc('E', 'C', 'S', '1');
constexpr uint32_t kPhys = fourcc('P', 'H', 'Y', 'S');

constexpr uint32_t kOrder[] = {kText, kMatl, kMesh, kXfrm, kNode, kGobj, kRend,
                               kSkin, kAnim, kMrph, kLght, kCamr, kEcs1, kPhys};
constexpr int kOrderCount = 14;

struct W {
    std::vector<uint8_t> b;
    bool ok = true;

    void fail() { ok = false; }

    void raw(const void* p, size_t n) {
        if (!ok || n == 0)
            return;
        const size_t at = b.size();
        b.resize(at + n);
        if (p)
            std::memcpy(b.data() + at, p, n);
    }
    void u8(uint8_t v) { raw(&v, 1); }
    void u32(uint32_t v) { raw(&v, 4); }
    void u64(uint64_t v) { raw(&v, 8); }
    void i32(int32_t v) { raw(&v, 4); }
    void f32(float v) { raw(&v, 4); }
    void str(const std::string& s) {
        if (s.size() > kMaxString) {
            fail();
            return;
        }
        u32(static_cast<uint32_t>(s.size()));
        raw(s.data(), s.size());
    }
    void v3(const glm::vec3& v) {
        f32(v.x);
        f32(v.y);
        f32(v.z);
    }
    void quat(const glm::quat& q) {
        f32(q.x);
        f32(q.y);
        f32(q.z);
        f32(q.w);
    }
    void m4(const glm::mat4& m) {
        for (int c = 0; c < 4; ++c)
            for (int r = 0; r < 4; ++r)
                f32(m[c][r]);
    }
    void aabb(const core::AABB& a) {
        v3(a.min);
        v3(a.max);
    }
};

struct R {
    const uint8_t* p = nullptr;
    const uint8_t* end = nullptr;
    bool ok = true;

    bool need(size_t n) {
        if (!ok || p == nullptr || n > static_cast<size_t>(end - p)) {
            ok = false;
            return false;
        }
        return true;
    }
    uint8_t u8() {
        if (!need(1))
            return 0;
        return *p++;
    }
    uint32_t u32() {
        uint32_t v = 0;
        if (!need(4))
            return 0;
        std::memcpy(&v, p, 4);
        p += 4;
        return v;
    }
    uint64_t u64() {
        uint64_t v = 0;
        if (!need(8))
            return 0;
        std::memcpy(&v, p, 8);
        p += 8;
        return v;
    }
    int32_t i32() {
        int32_t v = 0;
        if (!need(4))
            return 0;
        std::memcpy(&v, p, 4);
        p += 4;
        return v;
    }
    float f32() {
        float v = 0.f;
        if (!need(4))
            return 0.f;
        std::memcpy(&v, p, 4);
        p += 4;
        return v;
    }
    std::string str() {
        const uint32_t n = u32();
        if (!ok || n > kMaxString || !need(n)) {
            ok = false;
            return {};
        }
        std::string s(reinterpret_cast<const char*>(p), n);
        p += n;
        return s;
    }
    glm::vec3 v3() {
        glm::vec3 v;
        v.x = f32();
        v.y = f32();
        v.z = f32();
        return v;
    }
    glm::quat quat() {
        glm::quat q;
        q.x = f32();
        q.y = f32();
        q.z = f32();
        q.w = f32();
        return q;
    }
    glm::mat4 m4() {
        glm::mat4 m(1.0f);
        for (int c = 0; c < 4; ++c)
            for (int r = 0; r < 4; ++r)
                m[c][r] = f32();
        return m;
    }
    core::AABB aabb() {
        core::AABB a;
        a.min = v3();
        a.max = v3();
        return a;
    }
    bool done() const { return ok && p == end; }

    bool take(void* dst, size_t n) {
        if (!need(n))
            return false;
        if (n)
            std::memcpy(dst, p, n);
        p += n;
        return true;
    }
};

uint32_t fbits(float f) {
    uint32_t u = 0;
    std::memcpy(&u, &f, sizeof(u));
    return u;
}

bool count_ok(uint32_t n, uint32_t cap) { return n <= cap; }

struct Head {
    uint32_t flags = 0;
    float world_scale = 1.0f;
    uint64_t source_size = 0;
    uint64_t source_mtime = 0;
    uint32_t jolt = 0;
    std::string name;
};

// Consumes magic..name. Leaves the cursor on the section directory.
bool decode_fixed(R& r, Head& h) {
    const uint32_t magic = r.u32();
    const uint32_t version = r.u32();
    h.flags = r.u32();
    h.world_scale = r.f32();
    h.source_size = r.u64();
    h.source_mtime = r.u64();
    h.jolt = r.u32();
    const uint32_t name_len = r.u32();
    if (!r.ok || magic != kMagic || version != kNativeSceneVersion ||
        name_len > kMaxName || !r.need(name_len)) {
        r.ok = false;
        return false;
    }
    h.name.assign(reinterpret_cast<const char*>(r.p), name_len);
    r.p += name_len;
    return true;
}

bool stamp_matches(const Head& h, const std::string& scene_name, float world_scale,
                   uint32_t flags, uint64_t source_size, uint64_t source_mtime) {
    return h.name == scene_name && h.flags == flags &&
           fbits(h.world_scale) == fbits(world_scale) &&
           h.source_size == source_size && h.source_mtime == source_mtime &&
           h.jolt == physics::PhysicsWorld::jolt_binary_version();
}

void write_stamp(W& w, const std::string& scene_name, float world_scale, uint32_t flags,
                 uint64_t source_size, uint64_t source_mtime) {
    w.u32(kMagic);
    w.u32(kNativeSceneVersion);
    w.u32(flags);
    w.f32(world_scale);
    w.u64(source_size);
    w.u64(source_mtime);
    w.u32(physics::PhysicsWorld::jolt_binary_version());
    w.u32(static_cast<uint32_t>(scene_name.size()));
    w.raw(scene_name.data(), scene_name.size());
}

// One-shot helpers use a heap context. The MSVC zstd build sets
// ZSTD_HEAPMODE=0, and the one-shot ZSTD_compress puts that context
// on the stack.
bool zstd_compress(const std::vector<uint8_t>& raw, std::vector<uint8_t>& out) {
    const size_t bound = ZSTD_compressBound(raw.size());
    if (bound == 0 || bound > kMaxFile) {
        LOG_ERROR("[Native] zstd bound rejected");
        return false;
    }
    out.resize(bound);
    ZSTD_CCtx* ctx = ZSTD_createCCtx();
    if (!ctx) {
        LOG_ERROR("[Native] zstd could not allocate a compress context");
        return false;
    }
    const size_t level = ZSTD_CCtx_setParameter(ctx, ZSTD_c_compressionLevel, kZstdLevel);
    const size_t checksum = ZSTD_CCtx_setParameter(ctx, ZSTD_c_checksumFlag, 1);
    size_t n = 0;
    if (!ZSTD_isError(level) && !ZSTD_isError(checksum))
        n = ZSTD_compress2(ctx, out.data(), out.size(), raw.data(), raw.size());
    else
        n = level != 0 && ZSTD_isError(level) ? level : checksum;
    ZSTD_freeCCtx(ctx);
    if (ZSTD_isError(n) || n == 0 || n > out.size()) {
        LOG_ERROR("[Native] zstd compress failed: " << ZSTD_getErrorName(n));
        return false;
    }
    out.resize(n);
    return true;
}

bool zstd_decompress(const uint8_t* src, size_t comp_size, std::vector<uint8_t>& plain) {
    ZSTD_DCtx* ctx = ZSTD_createDCtx();
    if (!ctx) {
        LOG_ERROR("[Native] zstd could not allocate a decompress context");
        return false;
    }
    const size_t n = ZSTD_decompressDCtx(ctx, plain.data(), plain.size(), src, comp_size);
    ZSTD_freeDCtx(ctx);
    if (ZSTD_isError(n) || n != plain.size()) {
        LOG_ERROR("[Native] zstd decompress failed: "
                  << (ZSTD_isError(n) ? ZSTD_getErrorName(n) : "size mismatch"));
        return false;
    }
    return true;
}

bool write_text(W& w, const NativeSceneFile& scene) {
    if (!count_ok(static_cast<uint32_t>(scene.images.size()), kMaxImages)) {
        w.fail();
        return false;
    }
    w.u32(static_cast<uint32_t>(scene.images.size()));
    for (const gfx::CachedImage& img : scene.images) {
        if (img.name.empty() || img.width == 0 || img.height == 0 ||
            img.width > kMaxDim || img.height > kMaxDim) {
            w.fail();
            return false;
        }
        const uint64_t pix =
            static_cast<uint64_t>(img.width) * static_cast<uint64_t>(img.height);
        if (pix > static_cast<uint64_t>(kMaxDim) * kMaxDim ||
            pix * 4ull != img.rgba.size()) {
            w.fail();
            return false;
        }
        w.str(img.name);
        w.u32(img.width);
        w.u32(img.height);
        w.u8(img.srgb ? 1u : 0u);
        w.u8(0);
        w.u8(0);
        w.u8(0);
        w.u32(static_cast<uint32_t>(img.rgba.size()));
        w.raw(img.rgba.data(), img.rgba.size());
    }
    return w.ok;
}

bool read_text(R& r, NativeSceneFile& scene) {
    const uint32_t n = r.u32();
    if (!r.ok || !count_ok(n, kMaxImages))
        return false;
    scene.images.resize(n);
    for (uint32_t i = 0; i < n; ++i) {
        gfx::CachedImage img;
        img.name = r.str();
        img.width = r.u32();
        img.height = r.u32();
        img.srgb = r.u8();
        (void)r.u8();
        (void)r.u8();
        (void)r.u8();
        const uint32_t nbytes = r.u32();
        if (!r.ok || img.name.empty() || img.width == 0 || img.height == 0 ||
            img.width > kMaxDim || img.height > kMaxDim)
            return false;
        const uint64_t pix =
            static_cast<uint64_t>(img.width) * static_cast<uint64_t>(img.height);
        if (pix > static_cast<uint64_t>(kMaxDim) * kMaxDim || pix * 4ull != nbytes)
            return false;
        img.rgba.resize(nbytes);
        if (!r.take(img.rgba.data(), nbytes))
            return false;
        scene.images[i] = std::move(img);
    }
    return r.done();
}

bool write_matl(W& w, const NativeSceneFile& scene) {
    if (!count_ok(static_cast<uint32_t>(scene.materials.size()), kMaxCount)) {
        w.fail();
        return false;
    }
    w.u32(static_cast<uint32_t>(scene.materials.size()));
    if (!scene.materials.empty())
        w.raw(scene.materials.data(), scene.materials.size() * sizeof(gfx::Material));
    return w.ok;
}

bool read_matl(R& r, NativeSceneFile& scene) {
    const uint32_t n = r.u32();
    if (!r.ok || !count_ok(n, kMaxCount))
        return false;
    scene.materials.resize(n);
    if (n && !r.take(scene.materials.data(), static_cast<size_t>(n) * sizeof(gfx::Material)))
        return false;
    return r.done();
}

bool write_mesh(W& w, const NativeSceneFile& scene) {
    if (!count_ok(static_cast<uint32_t>(scene.meshes.size()), kMaxMeshes)) {
        w.fail();
        return false;
    }
    w.u32(static_cast<uint32_t>(scene.meshes.size()));
    for (const NativeMesh& mesh : scene.meshes) {
        if (mesh.vertices.size() > kMaxVerts || mesh.indices.size() > kMaxIndices ||
            mesh.meshlets.size() > kMaxVerts) {
            w.fail();
            return false;
        }
        w.u32(static_cast<uint32_t>(mesh.vertices.size()));
        w.u32(static_cast<uint32_t>(mesh.indices.size()));
        w.u32(static_cast<uint32_t>(mesh.meshlets.size()));
        w.u8(mesh.index16 ? 1u : 0u);
        w.u8(mesh.allow_meshlet_cull ? 1u : 0u);
        w.u8(0);
        w.u8(0);
        w.aabb(mesh.local_aabb);
        if (!mesh.vertices.empty())
            w.raw(mesh.vertices.data(), mesh.vertices.size() * sizeof(gfx::Vertex));
        if (!mesh.indices.empty())
            w.raw(mesh.indices.data(), mesh.indices.size() * sizeof(uint32_t));
        if (!mesh.meshlets.empty())
            w.raw(mesh.meshlets.data(), mesh.meshlets.size() * sizeof(gfx::MeshletDesc));
    }
    return w.ok;
}

bool read_mesh(R& r, NativeSceneFile& scene) {
    const uint32_t n = r.u32();
    if (!r.ok || !count_ok(n, kMaxMeshes))
        return false;
    scene.meshes.resize(n);
    for (uint32_t i = 0; i < n; ++i) {
        NativeMesh mesh;
        const uint32_t nv = r.u32();
        const uint32_t ni = r.u32();
        const uint32_t nm = r.u32();
        const uint8_t i16 = r.u8();
        const uint8_t cull = r.u8();
        (void)r.u8();
        (void)r.u8();
        mesh.local_aabb = r.aabb();
        if (!r.ok || nv > kMaxVerts || ni > kMaxIndices || nm > kMaxVerts)
            return false;
        mesh.vertices.resize(nv);
        mesh.indices.resize(ni);
        mesh.meshlets.resize(nm);
        if (nv && !r.take(mesh.vertices.data(), static_cast<size_t>(nv) * sizeof(gfx::Vertex)))
            return false;
        if (ni && !r.take(mesh.indices.data(), static_cast<size_t>(ni) * sizeof(uint32_t)))
            return false;
        if (nm &&
            !r.take(mesh.meshlets.data(), static_cast<size_t>(nm) * sizeof(gfx::MeshletDesc)))
            return false;
        mesh.index16 = i16 != 0;
        mesh.allow_meshlet_cull = cull != 0;
        scene.meshes[i] = std::move(mesh);
    }
    return r.done();
}

bool write_xfrm(W& w, const NativeSceneFile& scene) {
    if (!count_ok(static_cast<uint32_t>(scene.xforms.size()), kMaxCount)) {
        w.fail();
        return false;
    }
    w.u32(static_cast<uint32_t>(scene.xforms.size()));
    for (const NativeXform& x : scene.xforms) {
        w.u32(x.parent);
        w.v3(x.trs.translation);
        w.quat(x.trs.rotation);
        w.v3(x.trs.scale);
    }
    return w.ok;
}

bool read_xfrm(R& r, NativeSceneFile& scene) {
    const uint32_t n = r.u32();
    if (!r.ok || !count_ok(n, kMaxCount))
        return false;
    scene.xforms.resize(n);
    for (uint32_t i = 0; i < n; ++i) {
        NativeXform x;
        x.parent = r.u32();
        x.trs.translation = r.v3();
        x.trs.rotation = r.quat();
        x.trs.scale = r.v3();
        if (!r.ok)
            return false;
        if (x.parent != TransformManager::kInvalid && x.parent >= n)
            return false;
        scene.xforms[i] = x;
    }
    return r.done();
}

bool write_node(W& w, const NativeSceneFile& scene) {
    if (scene.node_names.size() != scene.node_to_xform.size()) {
        w.fail();
        return false;
    }
    if (!count_ok(static_cast<uint32_t>(scene.node_to_xform.size()), kMaxCount)) {
        w.fail();
        return false;
    }
    w.u32(static_cast<uint32_t>(scene.node_to_xform.size()));
    for (uint32_t id : scene.node_to_xform)
        w.u32(id);
    w.u32(static_cast<uint32_t>(scene.node_names.size()));
    for (const std::string& name : scene.node_names)
        w.str(name);
    return w.ok;
}

bool read_node(R& r, NativeSceneFile& scene) {
    const uint32_t n = r.u32();
    if (!r.ok || !count_ok(n, kMaxCount))
        return false;
    scene.node_to_xform.resize(n);
    for (uint32_t i = 0; i < n; ++i)
        scene.node_to_xform[i] = r.u32();
    const uint32_t nn = r.u32();
    if (!r.ok || nn != n)
        return false;
    scene.node_names.resize(nn);
    for (uint32_t i = 0; i < nn; ++i)
        scene.node_names[i] = r.str();
    return r.done();
}

bool write_gobj(W& w, const NativeSceneFile& scene) {
    if (!count_ok(static_cast<uint32_t>(scene.game_objects.size()), kMaxCount)) {
        w.fail();
        return false;
    }
    w.u32(static_cast<uint32_t>(scene.game_objects.size()));
    for (const GameObject& go : scene.game_objects) {
        w.u32(go.root_transform_index);
        w.u32(go.skin_index);
        w.u32(go.first_render_mesh);
        w.u32(go.render_mesh_count);
        w.u32(go.flags);
        w.u32(go.gltf_node_index);
        w.str(go.name);
    }
    return w.ok;
}

bool read_gobj(R& r, NativeSceneFile& scene) {
    const uint32_t n = r.u32();
    if (!r.ok || !count_ok(n, kMaxCount))
        return false;
    scene.game_objects.resize(n);
    for (uint32_t i = 0; i < n; ++i) {
        GameObject go;
        go.root_transform_index = r.u32();
        go.skin_index = r.u32();
        go.first_render_mesh = r.u32();
        go.render_mesh_count = r.u32();
        go.flags = r.u32();
        go.gltf_node_index = r.u32();
        go.name = r.str();
        if (!r.ok)
            return false;
        scene.game_objects[i] = std::move(go);
    }
    return r.done();
}

bool write_rend(W& w, const NativeSceneFile& scene) {
    if (!count_ok(static_cast<uint32_t>(scene.render_meshes.size()), kMaxCount)) {
        w.fail();
        return false;
    }
    w.u32(static_cast<uint32_t>(scene.render_meshes.size()));
    for (const RenderMesh& rm : scene.render_meshes) {
        w.u32(rm.game_object_index);
        w.u32(rm.mesh_index);
        w.u32(rm.material_index);
        w.u32(rm.transform_index);
        w.u32(rm.skin_index);
        w.aabb(rm.local_aabb);
        w.u32(rm.flags);
    }
    return w.ok;
}

bool read_rend(R& r, NativeSceneFile& scene) {
    const uint32_t n = r.u32();
    if (!r.ok || !count_ok(n, kMaxCount))
        return false;
    scene.render_meshes.resize(n);
    for (uint32_t i = 0; i < n; ++i) {
        RenderMesh rm;
        rm.game_object_index = r.u32();
        rm.mesh_index = r.u32();
        rm.material_index = r.u32();
        rm.transform_index = r.u32();
        rm.skin_index = r.u32();
        rm.local_aabb = r.aabb();
        rm.flags = r.u32();
        if (!r.ok)
            return false;
        scene.render_meshes[i] = rm;
    }
    return r.done();
}

bool write_skin(W& w, const NativeSceneFile& scene) {
    if (!count_ok(static_cast<uint32_t>(scene.skins.size()), kMaxCount)) {
        w.fail();
        return false;
    }
    w.u32(static_cast<uint32_t>(scene.skins.size()));
    for (const Skin& skin : scene.skins) {
        if (skin.joint_transform_indices.size() > kMaxJointsTotal ||
            skin.inverse_bind_matrices.size() > kMaxJointsTotal) {
            w.fail();
            return false;
        }
        w.u32(static_cast<uint32_t>(skin.joint_transform_indices.size()));
        for (uint32_t j : skin.joint_transform_indices)
            w.u32(j);
        w.u32(static_cast<uint32_t>(skin.inverse_bind_matrices.size()));
        for (const glm::mat4& m : skin.inverse_bind_matrices)
            w.m4(m);
        w.u32(skin.mesh_transform_index);
    }
    return w.ok;
}

bool read_skin(R& r, NativeSceneFile& scene) {
    const uint32_t n = r.u32();
    if (!r.ok || !count_ok(n, kMaxCount))
        return false;
    scene.skins.resize(n);
    for (uint32_t i = 0; i < n; ++i) {
        Skin skin;
        const uint32_t nj = r.u32();
        if (!r.ok || nj > kMaxJointsTotal)
            return false;
        skin.joint_transform_indices.resize(nj);
        for (uint32_t j = 0; j < nj; ++j)
            skin.joint_transform_indices[j] = r.u32();
        const uint32_t nm = r.u32();
        if (!r.ok || nm > kMaxJointsTotal)
            return false;
        skin.inverse_bind_matrices.resize(nm);
        for (uint32_t m = 0; m < nm; ++m)
            skin.inverse_bind_matrices[m] = r.m4();
        skin.mesh_transform_index = r.u32();
        if (!r.ok)
            return false;
        scene.skins[i] = std::move(skin);
    }
    return r.done();
}

bool write_anim(W& w, const NativeSceneFile& scene) {
    if (!count_ok(static_cast<uint32_t>(scene.clips.size()), kMaxCount)) {
        w.fail();
        return false;
    }
    w.u32(static_cast<uint32_t>(scene.clips.size()));
    for (const AnimationClip& clip : scene.clips) {
        if (!count_ok(static_cast<uint32_t>(clip.samplers.size()), kMaxCount) ||
            !count_ok(static_cast<uint32_t>(clip.channels.size()), kMaxCount)) {
            w.fail();
            return false;
        }
        w.str(clip.name);
        w.f32(clip.duration);
        w.u32(static_cast<uint32_t>(clip.samplers.size()));
        for (const AnimationSamplerData& s : clip.samplers) {
            if (s.times.size() > kMaxVerts || s.values.size() > kMaxVerts) {
                w.fail();
                return false;
            }
            w.u8(static_cast<uint8_t>(s.interp));
            w.u8(0);
            w.u8(0);
            w.u8(0);
            w.u32(s.component_count);
            w.u32(static_cast<uint32_t>(s.times.size()));
            if (!s.times.empty())
                w.raw(s.times.data(), s.times.size() * sizeof(float));
            w.u32(static_cast<uint32_t>(s.values.size()));
            if (!s.values.empty())
                w.raw(s.values.data(), s.values.size() * sizeof(float));
        }
        w.u32(static_cast<uint32_t>(clip.channels.size()));
        for (const AnimationChannel& ch : clip.channels) {
            w.u32(ch.transform_index);
            w.u32(ch.morph_index);
            w.u32(ch.sampler_index);
            w.u8(static_cast<uint8_t>(ch.path));
            w.u8(0);
            w.u8(0);
            w.u8(0);
        }
    }
    return w.ok;
}

bool read_anim(R& r, NativeSceneFile& scene) {
    const uint32_t n = r.u32();
    if (!r.ok || !count_ok(n, kMaxCount))
        return false;
    scene.clips.resize(n);
    for (uint32_t i = 0; i < n; ++i) {
        AnimationClip clip;
        clip.name = r.str();
        clip.duration = r.f32();
        const uint32_t ns = r.u32();
        if (!r.ok || !count_ok(ns, kMaxCount))
            return false;
        clip.samplers.resize(ns);
        for (uint32_t s = 0; s < ns; ++s) {
            AnimationSamplerData samp;
            const uint8_t interp = r.u8();
            (void)r.u8();
            (void)r.u8();
            (void)r.u8();
            samp.component_count = r.u32();
            const uint32_t nt = r.u32();
            if (!r.ok || interp > 2 || nt > kMaxVerts)
                return false;
            samp.interp = static_cast<AnimationInterp>(interp);
            samp.times.resize(nt);
            if (nt && !r.take(samp.times.data(), static_cast<size_t>(nt) * sizeof(float)))
                return false;
            const uint32_t nv = r.u32();
            if (!r.ok || nv > kMaxVerts)
                return false;
            samp.values.resize(nv);
            if (nv && !r.take(samp.values.data(), static_cast<size_t>(nv) * sizeof(float)))
                return false;
            clip.samplers[s] = std::move(samp);
        }
        const uint32_t nc = r.u32();
        if (!r.ok || !count_ok(nc, kMaxCount))
            return false;
        clip.channels.resize(nc);
        for (uint32_t c = 0; c < nc; ++c) {
            AnimationChannel ch;
            ch.transform_index = r.u32();
            ch.morph_index = r.u32();
            ch.sampler_index = r.u32();
            const uint8_t path = r.u8();
            (void)r.u8();
            (void)r.u8();
            (void)r.u8();
            if (!r.ok || path > 3)
                return false;
            ch.path = static_cast<AnimationPath>(path);
            clip.channels[c] = ch;
        }
        scene.clips[i] = std::move(clip);
    }
    return r.done();
}

bool write_vec3s(W& w, const std::vector<glm::vec3>& v) {
    if (v.size() > kMaxVerts) {
        w.fail();
        return false;
    }
    w.u32(static_cast<uint32_t>(v.size()));
    for (const glm::vec3& p : v)
        w.v3(p);
    return w.ok;
}

bool read_vec3s(R& r, std::vector<glm::vec3>& v) {
    const uint32_t n = r.u32();
    if (!r.ok || n > kMaxVerts)
        return false;
    v.resize(n);
    for (uint32_t i = 0; i < n; ++i)
        v[i] = r.v3();
    return r.ok;
}

bool write_mrph(W& w, const NativeSceneFile& scene) {
    if (!count_ok(static_cast<uint32_t>(scene.morphs.size()), kMaxCount) ||
        !count_ok(static_cast<uint32_t>(scene.node_to_morph.size()), kMaxCount)) {
        w.fail();
        return false;
    }
    w.u32(static_cast<uint32_t>(scene.morphs.size()));
    for (const MorphInstance& m : scene.morphs) {
        w.u32(m.mesh_primitive_index);
        w.u32(m.vertex_count);
        w.u32(m.target_count);
        w.u32(m.gltf_node_index);
        if (!write_vec3s(w, m.base_positions) || !write_vec3s(w, m.base_normals) ||
            !write_vec3s(w, m.target_positions) || !write_vec3s(w, m.target_normals))
            return false;
        if (m.weights.size() > kMaxVerts) {
            w.fail();
            return false;
        }
        w.u32(static_cast<uint32_t>(m.weights.size()));
        if (!m.weights.empty())
            w.raw(m.weights.data(), m.weights.size() * sizeof(float));
        w.u8(m.dirty ? 1u : 0u);
        w.u8(0);
        w.u8(0);
        w.u8(0);
    }
    w.u32(static_cast<uint32_t>(scene.node_to_morph.size()));
    for (uint32_t id : scene.node_to_morph)
        w.u32(id);
    return w.ok;
}

bool read_mrph(R& r, NativeSceneFile& scene) {
    const uint32_t n = r.u32();
    if (!r.ok || !count_ok(n, kMaxCount))
        return false;
    scene.morphs.resize(n);
    for (uint32_t i = 0; i < n; ++i) {
        MorphInstance m;
        m.mesh_primitive_index = r.u32();
        m.vertex_count = r.u32();
        m.target_count = r.u32();
        m.gltf_node_index = r.u32();
        if (!read_vec3s(r, m.base_positions) || !read_vec3s(r, m.base_normals) ||
            !read_vec3s(r, m.target_positions) || !read_vec3s(r, m.target_normals))
            return false;
        const uint32_t nw = r.u32();
        if (!r.ok || nw > kMaxVerts)
            return false;
        m.weights.resize(nw);
        if (nw && !r.take(m.weights.data(), static_cast<size_t>(nw) * sizeof(float)))
            return false;
        m.dirty = r.u8() != 0;
        (void)r.u8();
        (void)r.u8();
        (void)r.u8();
        if (!r.ok)
            return false;
        scene.morphs[i] = std::move(m);
    }
    const uint32_t nn = r.u32();
    if (!r.ok || !count_ok(nn, kMaxCount))
        return false;
    scene.node_to_morph.resize(nn);
    for (uint32_t i = 0; i < nn; ++i)
        scene.node_to_morph[i] = r.u32();
    return r.done();
}

void write_light(W& w, const gfx::Light& L) {
    w.u32(static_cast<uint32_t>(L.type));
    w.v3(L.position);
    w.v3(L.direction);
    w.v3(L.color);
    w.f32(L.intensity);
    w.f32(L.range);
    w.f32(L.innerConeAngle);
    w.f32(L.outerConeAngle);
    w.u32(L.transform_index);
    w.u8(L.enabled ? 1u : 0u);
    w.u8(0);
    w.u8(0);
    w.u8(0);
}

bool read_light(R& r, gfx::Light& L) {
    const uint32_t type = r.u32();
    L.position = r.v3();
    L.direction = r.v3();
    L.color = r.v3();
    L.intensity = r.f32();
    L.range = r.f32();
    L.innerConeAngle = r.f32();
    L.outerConeAngle = r.f32();
    L.transform_index = r.u32();
    const uint8_t en = r.u8();
    (void)r.u8();
    (void)r.u8();
    (void)r.u8();
    if (!r.ok || type > 2)
        return false;
    L.type = static_cast<gfx::LightType>(type);
    L.enabled = en != 0;
    return true;
}

bool write_lght(W& w, const NativeSceneFile& scene) {
    if (!count_ok(static_cast<uint32_t>(scene.lights.size()), kMaxCount)) {
        w.fail();
        return false;
    }
    w.u32(static_cast<uint32_t>(scene.lights.size()));
    for (const gfx::Light& L : scene.lights)
        write_light(w, L);
    write_light(w, scene.global_light);
    return w.ok;
}

bool read_lght(R& r, NativeSceneFile& scene) {
    const uint32_t n = r.u32();
    if (!r.ok || !count_ok(n, kMaxCount))
        return false;
    scene.lights.resize(n);
    for (uint32_t i = 0; i < n; ++i) {
        if (!read_light(r, scene.lights[i]))
            return false;
    }
    return read_light(r, scene.global_light) && r.done();
}

bool write_camr(W& w, const NativeSceneFile& scene) {
    const NativeCamera& c = scene.camera;
    w.u8(c.valid ? 1u : 0u);
    w.u8(0);
    w.u8(0);
    w.u8(0);
    w.f32(c.yfov);
    w.f32(c.aspect);
    w.f32(c.znear);
    w.f32(c.zfar);
    w.i32(c.gltf_index);
    w.u32(c.transform_index);
    return w.ok;
}

bool read_camr(R& r, NativeSceneFile& scene) {
    NativeCamera c;
    c.valid = r.u8() != 0;
    (void)r.u8();
    (void)r.u8();
    (void)r.u8();
    c.yfov = r.f32();
    c.aspect = r.f32();
    c.znear = r.f32();
    c.zfar = r.f32();
    c.gltf_index = r.i32();
    c.transform_index = r.u32();
    if (!r.ok)
        return false;
    scene.camera = c;
    return r.done();
}

bool entity_ok(uint32_t e, uint32_t count) { return e < count; }

bool write_fps(W& w, const ecs::FpsMove& m) {
    w.f32(m.yaw_deg);
    w.f32(m.pitch_deg);
    w.f32(m.vertical_velocity);
    w.f32(m.ground_y);
    w.u8(m.grounded ? 1u : 0u);
    w.u8(m.crouching ? 1u : 0u);
    w.u8(m.initialized ? 1u : 0u);
    w.u8(0);
    w.f32(m.jump_speed);
    w.f32(m.gravity);
    w.f32(m.crouch_eye_scale);
    w.f32(m.crouch_speed_scale);
    w.f32(m.pitch_min);
    w.f32(m.pitch_max);
    w.quat(m.body_rest_rotation);
    w.u8(m.body_rest_captured ? 1u : 0u);
    w.u8(0);
    w.u8(0);
    w.u8(0);
    w.f32(m.horizontal_speed);
    return w.ok;
}

bool read_fps(R& r, ecs::FpsMove& m) {
    m.yaw_deg = r.f32();
    m.pitch_deg = r.f32();
    m.vertical_velocity = r.f32();
    m.ground_y = r.f32();
    m.grounded = r.u8() != 0;
    m.crouching = r.u8() != 0;
    m.initialized = r.u8() != 0;
    (void)r.u8();
    m.jump_speed = r.f32();
    m.gravity = r.f32();
    m.crouch_eye_scale = r.f32();
    m.crouch_speed_scale = r.f32();
    m.pitch_min = r.f32();
    m.pitch_max = r.f32();
    m.body_rest_rotation = r.quat();
    m.body_rest_captured = r.u8() != 0;
    (void)r.u8();
    (void)r.u8();
    (void)r.u8();
    m.horizontal_speed = r.f32();
    return r.ok;
}

bool write_rig(W& w, const ecs::CameraRig& m) {
    w.f32(m.movement_speed);
    w.f32(m.mouse_sensitivity);
    w.f32(m.roll_speed);
    w.f32(m.fov_degrees);
    w.f32(m.near_plane);
    w.f32(m.far_plane);
    w.u8(m.invert_pitch ? 1u : 0u);
    w.u8(0);
    w.u8(0);
    w.u8(0);
    w.v3(m.eye_offset);
    w.u8(m.third_person ? 1u : 0u);
    w.u8(0);
    w.u8(0);
    w.u8(0);
    w.v3(m.boom_offset);
    w.f32(m.body_yaw_offset_deg);
    return w.ok;
}

bool read_rig(R& r, ecs::CameraRig& m) {
    m.movement_speed = r.f32();
    m.mouse_sensitivity = r.f32();
    m.roll_speed = r.f32();
    m.fov_degrees = r.f32();
    m.near_plane = r.f32();
    m.far_plane = r.f32();
    m.invert_pitch = r.u8() != 0;
    (void)r.u8();
    (void)r.u8();
    (void)r.u8();
    m.eye_offset = r.v3();
    m.third_person = r.u8() != 0;
    (void)r.u8();
    (void)r.u8();
    (void)r.u8();
    m.boom_offset = r.v3();
    m.body_yaw_offset_deg = r.f32();
    return r.ok;
}

bool write_loco(W& w, const ecs::LocomotionAnim& m) {
    w.u32(m.idle_clip);
    w.u32(m.walk_clip);
    w.u32(m.run_clip);
    w.f32(m.walk_threshold);
    w.f32(m.run_threshold);
    w.f32(m.fade);
    w.u8(static_cast<uint8_t>(m.state));
    w.u8(m.clips_bound ? 1u : 0u);
    w.u8(0);
    w.u8(0);
    w.str(m.idle_name);
    w.str(m.walk_name);
    w.str(m.run_name);
    return w.ok;
}

bool read_loco(R& r, ecs::LocomotionAnim& m) {
    m.idle_clip = r.u32();
    m.walk_clip = r.u32();
    m.run_clip = r.u32();
    m.walk_threshold = r.f32();
    m.run_threshold = r.f32();
    m.fade = r.f32();
    const uint8_t st = r.u8();
    m.clips_bound = r.u8() != 0;
    (void)r.u8();
    (void)r.u8();
    m.idle_name = r.str();
    m.walk_name = r.str();
    m.run_name = r.str();
    if (!r.ok || st > 2)
        return false;
    m.state = static_cast<ecs::LocomotionAnim::State>(st);
    return true;
}

bool write_ecs(W& w, const NativeSceneFile& scene) {
    const NativeEcs& e = scene.ecs;
    if (!count_ok(e.entity_count, kMaxCount) || e.alive.size() != e.entity_count) {
        w.fail();
        return false;
    }
    auto n_ok = [](size_t n) { return n <= kMaxCount; };
    if (!n_ok(e.transform_links.size()) || !n_ok(e.player_tags.size()) ||
        !n_ok(e.desktop_moves.size()) || !n_ok(e.fps_moves.size()) ||
        !n_ok(e.camera_rigs.size()) || !n_ok(e.healths.size()) ||
        !n_ok(e.names.size()) || !n_ok(e.scripts.size()) ||
        !n_ok(e.locomotion.size()) || !n_ok(e.gltf_node_to_entity.size()) ||
        !n_ok(e.game_object_to_entity.size())) {
        w.fail();
        return false;
    }
    w.u32(e.entity_count);
    for (uint8_t a : e.alive)
        w.u8(a ? 1u : 0u);
    w.u32(static_cast<uint32_t>(e.transform_links.size()));
    for (const auto& it : e.transform_links) {
        w.u32(it.first);
        w.u32(it.second.transform_index);
    }
    w.u32(static_cast<uint32_t>(e.player_tags.size()));
    for (ecs::Entity id : e.player_tags)
        w.u32(id);
    w.u32(static_cast<uint32_t>(e.desktop_moves.size()));
    for (ecs::Entity id : e.desktop_moves)
        w.u32(id);
    w.u32(static_cast<uint32_t>(e.fps_moves.size()));
    for (const auto& it : e.fps_moves) {
        w.u32(it.first);
        write_fps(w, it.second);
    }
    w.u32(static_cast<uint32_t>(e.camera_rigs.size()));
    for (const auto& it : e.camera_rigs) {
        w.u32(it.first);
        write_rig(w, it.second);
    }
    w.u32(static_cast<uint32_t>(e.healths.size()));
    for (const auto& it : e.healths) {
        w.u32(it.first);
        w.f32(it.second.current);
        w.f32(it.second.max);
    }
    w.u32(static_cast<uint32_t>(e.names.size()));
    for (const auto& it : e.names) {
        w.u32(it.first);
        w.str(it.second.value);
    }
    w.u32(static_cast<uint32_t>(e.scripts.size()));
    for (const auto& it : e.scripts) {
        w.u32(it.first);
        w.str(it.second);
    }
    w.u32(static_cast<uint32_t>(e.locomotion.size()));
    for (const auto& it : e.locomotion) {
        w.u32(it.first);
        write_loco(w, it.second);
    }
    w.u32(static_cast<uint32_t>(e.gltf_node_to_entity.size()));
    for (ecs::Entity id : e.gltf_node_to_entity)
        w.u32(id);
    w.u32(static_cast<uint32_t>(e.game_object_to_entity.size()));
    for (ecs::Entity id : e.game_object_to_entity)
        w.u32(id);
    return w.ok;
}

bool read_entities(R& r, uint32_t count, uint32_t n, std::vector<ecs::Entity>& out,
                   bool allow_invalid) {
    if (!count_ok(n, kMaxCount))
        return false;
    out.resize(n);
    for (uint32_t i = 0; i < n; ++i) {
        const uint32_t e = r.u32();
        if (!r.ok)
            return false;
        if (!allow_invalid && count > 0 && !entity_ok(e, count))
            return false;
        if (!allow_invalid && count == 0)
            return false;
        out[i] = e;
    }
    return true;
}

bool read_ecs(R& r, NativeSceneFile& scene) {
    NativeEcs e;
    e.entity_count = r.u32();
    if (!r.ok || !count_ok(e.entity_count, kMaxCount))
        return false;
    e.alive.resize(e.entity_count);
    for (uint32_t i = 0; i < e.entity_count; ++i)
        e.alive[i] = r.u8() ? 1u : 0u;
    const uint32_t nlink = r.u32();
    if (!r.ok || !count_ok(nlink, kMaxCount))
        return false;
    e.transform_links.resize(nlink);
    for (uint32_t i = 0; i < nlink; ++i) {
        const uint32_t id = r.u32();
        ecs::TransformLink link;
        link.transform_index = r.u32();
        if (!r.ok || !entity_ok(id, e.entity_count))
            return false;
        e.transform_links[i] = {id, link};
    }
    uint32_t n = r.u32();
    if (!read_entities(r, e.entity_count, n, e.player_tags, false))
        return false;
    n = r.u32();
    if (!read_entities(r, e.entity_count, n, e.desktop_moves, false))
        return false;
    n = r.u32();
    if (!r.ok || !count_ok(n, kMaxCount))
        return false;
    e.fps_moves.resize(n);
    for (uint32_t i = 0; i < n; ++i) {
        const uint32_t id = r.u32();
        ecs::FpsMove mv;
        if (!read_fps(r, mv) || !entity_ok(id, e.entity_count))
            return false;
        e.fps_moves[i] = {id, mv};
    }
    n = r.u32();
    if (!r.ok || !count_ok(n, kMaxCount))
        return false;
    e.camera_rigs.resize(n);
    for (uint32_t i = 0; i < n; ++i) {
        const uint32_t id = r.u32();
        ecs::CameraRig rig;
        if (!read_rig(r, rig) || !entity_ok(id, e.entity_count))
            return false;
        e.camera_rigs[i] = {id, rig};
    }
    n = r.u32();
    if (!r.ok || !count_ok(n, kMaxCount))
        return false;
    e.healths.resize(n);
    for (uint32_t i = 0; i < n; ++i) {
        const uint32_t id = r.u32();
        ecs::Health h;
        h.current = r.f32();
        h.max = r.f32();
        if (!r.ok || !entity_ok(id, e.entity_count))
            return false;
        e.healths[i] = {id, h};
    }
    n = r.u32();
    if (!r.ok || !count_ok(n, kMaxCount))
        return false;
    e.names.resize(n);
    for (uint32_t i = 0; i < n; ++i) {
        const uint32_t id = r.u32();
        ecs::Name name;
        name.value = r.str();
        if (!r.ok || !entity_ok(id, e.entity_count))
            return false;
        e.names[i] = {id, std::move(name)};
    }
    n = r.u32();
    if (!r.ok || !count_ok(n, kMaxCount))
        return false;
    e.scripts.resize(n);
    for (uint32_t i = 0; i < n; ++i) {
        const uint32_t id = r.u32();
        std::string script = r.str();
        if (!r.ok || !entity_ok(id, e.entity_count))
            return false;
        e.scripts[i] = {id, std::move(script)};
    }
    n = r.u32();
    if (!r.ok || !count_ok(n, kMaxCount))
        return false;
    e.locomotion.resize(n);
    for (uint32_t i = 0; i < n; ++i) {
        const uint32_t id = r.u32();
        ecs::LocomotionAnim loco;
        if (!read_loco(r, loco) || !entity_ok(id, e.entity_count))
            return false;
        e.locomotion[i] = {id, std::move(loco)};
    }
    n = r.u32();
    if (!read_entities(r, e.entity_count, n, e.gltf_node_to_entity, true))
        return false;
    n = r.u32();
    if (!read_entities(r, e.entity_count, n, e.game_object_to_entity, true))
        return false;
    if (!r.done())
        return false;
    scene.ecs = std::move(e);
    return true;
}

bool write_phys(W& w, const NativeSceneFile& scene) {
    if (!count_ok(static_cast<uint32_t>(scene.bodies.size()), kMaxMeshes)) {
        w.fail();
        return false;
    }
    w.u32(static_cast<uint32_t>(scene.bodies.size()));
    for (const physics::PhysicsWorld::CookedBody& body : scene.bodies) {
        if (body.shape_bytes.size() > kMaxShape || body.shape_bytes.empty()) {
            w.fail();
            return false;
        }
        w.v3(body.pose.position);
        w.quat(body.pose.rotation);
        w.u8(static_cast<uint8_t>(body.pose.motion));
        w.u8(0);
        w.u8(0);
        w.u8(0);
        w.f32(body.pose.restitution);
        w.f32(body.pose.friction);
        w.f32(body.pose.mass);
        w.u32(body.pose.transform_index);
        w.u32(static_cast<uint32_t>(body.shape_bytes.size()));
        w.raw(body.shape_bytes.data(), body.shape_bytes.size());
    }
    return w.ok;
}

bool read_phys(R& r, NativeSceneFile& scene) {
    const uint32_t n = r.u32();
    if (!r.ok || !count_ok(n, kMaxMeshes))
        return false;
    scene.bodies.resize(n);
    for (uint32_t i = 0; i < n; ++i) {
        physics::PhysicsWorld::CookedBody body;
        body.pose.position = r.v3();
        body.pose.rotation = r.quat();
        const uint8_t motion = r.u8();
        (void)r.u8();
        (void)r.u8();
        (void)r.u8();
        body.pose.restitution = r.f32();
        body.pose.friction = r.f32();
        body.pose.mass = r.f32();
        body.pose.transform_index = r.u32();
        const uint32_t nb = r.u32();
        if (!r.ok || motion > 2 || nb == 0 || nb > kMaxShape)
            return false;
        body.pose.motion = static_cast<physics::MotionType>(motion);
        body.shape_bytes.resize(nb);
        if (!r.take(body.shape_bytes.data(), nb))
            return false;
        scene.bodies[i] = std::move(body);
    }
    return r.done();
}

using WriteFn = bool (*)(W&, const NativeSceneFile&);
using ReadFn = bool (*)(R&, NativeSceneFile&);

constexpr WriteFn kWriters[] = {write_text, write_matl, write_mesh, write_xfrm,
                                write_node, write_gobj, write_rend, write_skin,
                                write_anim, write_mrph, write_lght, write_camr,
                                write_ecs,  write_phys};
constexpr ReadFn kReaders[] = {read_text, read_matl, read_mesh, read_xfrm, read_node,
                               read_gobj, read_rend, read_skin, read_anim, read_mrph,
                               read_lght, read_camr, read_ecs,  read_phys};
constexpr const char* kNames[] = {"TEXT", "MATL", "MESH", "XFRM", "NODE", "GOBJ", "REND",
                                  "SKIN", "ANIM", "MRPH", "LGHT", "CAMR", "ECS1", "PHYS"};

bool commit_file(const std::filesystem::path& dest, const std::vector<uint8_t>& bytes) {
    std::error_code ec;
    if (dest.has_parent_path()) {
        std::filesystem::create_directories(dest.parent_path(), ec);
        if (ec) {
            LOG_ERROR("[Native] create_directories failed: " << ec.message());
            return false;
        }
    }
    std::filesystem::path tmp = dest;
    tmp += ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) {
            LOG_ERROR("[Native] could not open " << tmp.string());
            return false;
        }
        if (!bytes.empty())
            out.write(reinterpret_cast<const char*>(bytes.data()),
                      static_cast<std::streamsize>(bytes.size()));
        out.flush();
        if (!out) {
            LOG_ERROR("[Native] write failed " << tmp.string());
            return false;
        }
    }
    std::filesystem::remove(dest, ec);
    ec.clear();
    std::filesystem::rename(tmp, dest, ec);
    if (ec) {
        LOG_ERROR("[Native] rename failed: " << ec.message());
        std::filesystem::remove(tmp, ec);
        return false;
    }
    return true;
}

} // namespace

bool source_file_stamp(const std::filesystem::path& source, uint64_t& size,
                       uint64_t& mtime) {
    std::error_code ec;
    if (!std::filesystem::is_regular_file(source, ec))
        return false;
    const auto bytes = std::filesystem::file_size(source, ec);
    if (ec)
        return false;
    const auto ft = std::filesystem::last_write_time(source, ec);
    if (ec)
        return false;
    size = static_cast<uint64_t>(bytes);
    mtime = static_cast<uint64_t>(ft.time_since_epoch().count());
    return true;
}

std::filesystem::path native_cache_path(const std::string& scene_name) {
    std::string sanitized;
    sanitized.reserve(scene_name.size());
    for (unsigned char c : scene_name) {
        if (std::isalnum(c) || c == '_' || c == '-')
            sanitized.push_back(static_cast<char>(c));
        else
            sanitized.push_back('_');
    }
    if (sanitized.empty())
        sanitized = "scene";
    return std::filesystem::path("cache") / (sanitized + ".abn");
}

NativeStamp native_cache_stamp(const std::filesystem::path& file,
                               const std::string& scene_name, float world_scale,
                               uint32_t flags, uint64_t source_size,
                               uint64_t source_mtime) {
    std::error_code ec;
    if (!std::filesystem::is_regular_file(file, ec))
        return NativeStamp::Missing;
    std::ifstream in(file, std::ios::binary);
    if (!in)
        return NativeStamp::Missing;
    uint8_t fixed[40];
    in.read(reinterpret_cast<char*>(fixed), 40);
    if (in.gcount() != 40)
        return NativeStamp::Mismatch;
    R peek{fixed, fixed + 40, true};
    (void)peek.u32();
    (void)peek.u32();
    (void)peek.u32();
    (void)peek.f32();
    (void)peek.u64();
    (void)peek.u64();
    (void)peek.u32();
    const uint32_t name_len = peek.u32();
    if (!peek.ok || name_len > kMaxName)
        return NativeStamp::Mismatch;
    std::vector<uint8_t> prefix(40u + name_len);
    std::memcpy(prefix.data(), fixed, 40);
    if (name_len != 0) {
        in.read(reinterpret_cast<char*>(prefix.data() + 40), name_len);
        if (in.gcount() != static_cast<std::streamsize>(name_len))
            return NativeStamp::Mismatch;
    }
    R full{prefix.data(), prefix.data() + prefix.size(), true};
    Head head;
    if (!decode_fixed(full, head) || !full.done())
        return NativeStamp::Mismatch;
    if (!stamp_matches(head, scene_name, world_scale, flags, source_size, source_mtime))
        return NativeStamp::Mismatch;
    return NativeStamp::Match;
}

bool write_native_scene(const std::filesystem::path& file, const std::string& scene_name,
                        float world_scale, uint32_t flags, uint64_t source_size,
                        uint64_t source_mtime, const NativeSceneFile& scene) {
    if (scene_name.size() > kMaxName) {
        LOG_ERROR("[Native] scene name is too long to cache");
        return false;
    }
    W parts[kOrderCount];
    for (int i = 0; i < kOrderCount; ++i) {
        if (!kWriters[i](parts[i], scene) || !parts[i].ok) {
            LOG_ERROR("[Native] failed while writing section " << kNames[i]);
            return false;
        }
    }

    const uint64_t header = 40ull + scene_name.size() + 4ull +
                            static_cast<uint64_t>(kOrderCount) * 24ull;
    uint64_t cursor = header;
    uint64_t total = header;
    for (int i = 0; i < kOrderCount; ++i)
        total += parts[i].b.size();
    if (total > kMaxFile) {
        LOG_ERROR("[Native] cache would exceed the file size cap");
        return false;
    }

    W out;
    out.b.reserve(static_cast<size_t>(total));
    write_stamp(out, scene_name, world_scale, flags, source_size, source_mtime);
    out.u32(static_cast<uint32_t>(kOrderCount));
    for (int i = 0; i < kOrderCount; ++i) {
        out.u32(kOrder[i]);
        out.u32(0);
        out.u64(cursor);
        out.u64(parts[i].b.size());
        cursor += parts[i].b.size();
    }
    for (int i = 0; i < kOrderCount; ++i)
        out.raw(parts[i].b.data(), parts[i].b.size());
    if (!out.ok || out.b.size() != total) {
        LOG_ERROR("[Native] cache buffer assembly failed");
        return false;
    }

    const auto t_comp = std::chrono::steady_clock::now();
    std::vector<uint8_t> compressed;
    if (!zstd_compress(out.b, compressed))
        return false;
    const auto compress_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                 std::chrono::steady_clock::now() - t_comp)
                                 .count();

    W env;
    env.b.reserve(40u + scene_name.size() + 16u + compressed.size());
    write_stamp(env, scene_name, world_scale, flags, source_size, source_mtime);
    env.u64(out.b.size());
    env.u64(compressed.size());
    env.raw(compressed.data(), compressed.size());
    if (!env.ok) {
        LOG_ERROR("[Native] cache envelope assembly failed");
        return false;
    }

    const auto t0 = std::chrono::steady_clock::now();
    if (!commit_file(file, env.b))
        return false;
    const auto write_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                              std::chrono::steady_clock::now() - t0)
                              .count();
    LOG_INFO("[Native] wrote " << file.string() << " bytes=" << env.b.size()
                               << " raw_bytes=" << out.b.size() << " level=" << kZstdLevel
                               << " compress_ms=" << compress_ms << " write_ms=" << write_ms);
    return true;
}

bool parse_document(const std::vector<uint8_t>& buf, const std::string& scene_name,
                    float world_scale, uint32_t flags, uint64_t source_size,
                    uint64_t source_mtime, NativeSceneFile& scene) {
    R r{buf.data(), buf.data() + buf.size(), true};
    Head head;
    if (!decode_fixed(r, head)) {
        LOG_ERROR("[Native] cache header is invalid");
        return false;
    }
    if (!stamp_matches(head, scene_name, world_scale, flags, source_size, source_mtime)) {
        LOG_ERROR("[Native] cache stamp does not match the source file");
        return false;
    }

    const uint32_t sections = r.u32();
    if (!r.ok || sections != static_cast<uint32_t>(kOrderCount)) {
        LOG_ERROR("[Native] cache section count is " << sections);
        return false;
    }
    struct Ent {
        uint32_t id = 0;
        uint64_t offset = 0;
        uint64_t size = 0;
    };
    Ent dir[kOrderCount];
    const uint64_t header_end = static_cast<uint64_t>(r.p - buf.data()) +
                                static_cast<uint64_t>(kOrderCount) * 24ull;
    for (int i = 0; i < kOrderCount; ++i) {
        dir[i].id = r.u32();
        (void)r.u32();
        dir[i].offset = r.u64();
        dir[i].size = r.u64();
        if (!r.ok || dir[i].id != kOrder[i]) {
            LOG_ERROR("[Native] cache directory mismatch at " << kNames[i]);
            return false;
        }
        if (dir[i].offset < header_end || dir[i].offset > buf.size() ||
            dir[i].size > buf.size() - dir[i].offset) {
            LOG_ERROR("[Native] section " << kNames[i] << " is out of range");
            return false;
        }
    }
    if (static_cast<size_t>(r.p - buf.data()) != header_end) {
        LOG_ERROR("[Native] header size mismatch");
        return false;
    }

    for (int i = 0; i < kOrderCount; ++i) {
        R slice{buf.data() + dir[i].offset, buf.data() + dir[i].offset + dir[i].size,
                true};
        if (!kReaders[i](slice, scene)) {
            LOG_ERROR("[Native] failed reading section " << kNames[i]);
            scene = NativeSceneFile{};
            return false;
        }
    }
    return true;
}

bool read_native_scene(const std::filesystem::path& file, const std::string& scene_name,
                       float world_scale, uint32_t flags, uint64_t source_size,
                       uint64_t source_mtime, NativeSceneFile& scene) {
    scene = NativeSceneFile{};
    std::ifstream in(file, std::ios::binary | std::ios::ate);
    if (!in) {
        LOG_ERROR("[Native] could not open " << file.string());
        return false;
    }
    const std::streamoff end = in.tellg();
    if (end < 40 || static_cast<uint64_t>(end) > kMaxFile) {
        LOG_ERROR("[Native] cache file size rejected (" << end << ")");
        return false;
    }
    in.seekg(0);
    std::vector<uint8_t> file_bytes(static_cast<size_t>(end));
    if (!file_bytes.empty())
        in.read(reinterpret_cast<char*>(file_bytes.data()),
                static_cast<std::streamsize>(file_bytes.size()));
    if (!in) {
        LOG_ERROR("[Native] short read " << file.string());
        return false;
    }

    R r{file_bytes.data(), file_bytes.data() + file_bytes.size(), true};
    Head head;
    if (!decode_fixed(r, head)) {
        LOG_ERROR("[Native] cache header is invalid");
        return false;
    }
    if (!stamp_matches(head, scene_name, world_scale, flags, source_size, source_mtime)) {
        LOG_ERROR("[Native] cache stamp does not match the source file");
        return false;
    }
    const uint64_t raw_size = r.u64();
    const uint64_t comp_size = r.u64();
    if (!r.ok || raw_size < 40 || raw_size > kMaxFile || comp_size == 0 ||
        comp_size > kMaxFile || static_cast<uint64_t>(r.end - r.p) != comp_size) {
        LOG_ERROR("[Native] cache payload size rejected");
        return false;
    }

    const auto t0 = std::chrono::steady_clock::now();
    std::vector<uint8_t> plain(static_cast<size_t>(raw_size));
    if (!zstd_decompress(r.p, static_cast<size_t>(comp_size), plain))
        return false;
    const auto decompress_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                   std::chrono::steady_clock::now() - t0)
                                   .count();
    LOG_INFO("[Native] zstd raw_bytes=" << raw_size << " comp_bytes=" << comp_size
                                        << " decompress_ms=" << decompress_ms);

    if (!parse_document(plain, scene_name, world_scale, flags, source_size, source_mtime,
                        scene)) {
        scene = NativeSceneFile{};
        return false;
    }
    return true;
}

} // namespace scene
