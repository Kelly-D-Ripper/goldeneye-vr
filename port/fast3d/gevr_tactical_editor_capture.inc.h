/* Record the posed actor through the renderer's decoded vertices and textures. */
#include "gevr_tactical_editor_capture.h"

static bool s_editorCapture;
static float s_editorViewToModel[4][4];
static float s_editorBones[96][4][4];
static uintptr_t s_editorBoneAddress;
static int s_editorBoneCount, s_editorBone;
static float s_editorPositions[MAX_VERTICES + 4][3];
static bool s_editorVertexOwned[MAX_VERTICES + 4];
struct EditorCorner {
    float position[3], uv[2];
    uint8_t rgba[4], texture, padding[3];
};
struct EditorPicture {
    uint32_t id;
    uint16_t width, height;
    std::vector<uint8_t> pixels;
};
static std::vector<EditorCorner> s_editorCorners;
static std::vector<EditorPicture> s_editorPictures;
extern bool gevrEditorReadTexture(uint32_t id, uint16_t &w, uint16_t &h, std::vector<uint8_t> &pixels);

extern "C" void gevrEditorCaptureSetTransform(const float viewToModel[4][4], const void *bones, int count)
{
    memcpy(s_editorViewToModel, viewToModel, sizeof(s_editorViewToModel));
    s_editorBoneCount = std::min(count, (int)(sizeof(s_editorBones) / sizeof(s_editorBones[0])));
    s_editorBoneAddress = (uintptr_t)bones;
    memcpy(s_editorBones, bones, (size_t)s_editorBoneCount * sizeof(s_editorBones[0]));
}

static void gevrEditorCaptureStart()
{
    s_editorCapture = true;
    s_editorBone = -1;
    memset(s_editorVertexOwned, 0, sizeof(s_editorVertexOwned));
    s_editorCorners.clear();
    s_editorPictures.clear();
    s_editorPictures.push_back({0, 1, 1, {255,255,255,255}});
}

static void gevrEditorCaptureEnd()
{
    s_editorCapture = false;
    if (s_editorCorners.empty()) return;
    FILE *file = fopen("npc-preview.bin", "wb");
    if (!file) return;
    fwrite("GEVRWPN1", 8, 1, file);
    uint32_t count = (uint32_t)s_editorPictures.size();
    fwrite(&count, 4, 1, file);
    for (const auto &picture : s_editorPictures) {
        fwrite(&picture.width, 2, 1, file);
        fwrite(&picture.height, 2, 1, file);
        fwrite(picture.pixels.data(), 1, picture.pixels.size(), file);
    }
    count = (uint32_t)s_editorCorners.size() / 3;
    fwrite(&count, 4, 1, file);
    static_assert(sizeof(EditorCorner) == 28, "GEVRWPN1 vertex layout");
    fwrite(s_editorCorners.data(), sizeof(EditorCorner), s_editorCorners.size(), file);
    fclose(file);
    sysLogPrintf(LOG_NOTE, "loadout editor preview: exported %u actor triangles", count);
}

static uint8_t gevrEditorTexture(uint32_t id)
{
    for (size_t i = 0; i < s_editorPictures.size(); ++i)
        if (s_editorPictures[i].id == id) return (uint8_t)i;
    EditorPicture picture = {id, 0, 0, {}};
    if (s_editorPictures.size() >= 64 || !gevrEditorReadTexture(id, picture.width, picture.height, picture.pixels))
        return 0;
    s_editorPictures.emplace_back(std::move(picture));
    return (uint8_t)(s_editorPictures.size() - 1);
}

static void gevrEditorCapturePosition(size_t index, const float p[3])
{
    s_editorVertexOwned[index] = s_editorBone >= 0;
    if (s_editorBone < 0) return;
    const auto &model = s_editorBones[s_editorBone];
    float world[3];
    for (int j = 0; j < 3; ++j)
        world[j] = p[0]*model[0][j] + p[1]*model[1][j] + p[2]*model[2][j] + model[3][j];
    for (int j = 0; j < 3; ++j)
        s_editorPositions[index][j] = world[0]*s_editorViewToModel[0][j] + world[1]*s_editorViewToModel[1][j]
            + world[2]*s_editorViewToModel[2][j] + s_editorViewToModel[3][j];
}

static void gevrEditorCaptureTriangle(const uint8_t index[3], uint32_t textureId,
    const float scale[2], const float origin[2], const float size[2], bool half, bool centre, bool alpha)
{
    if (s_editorCorners.size() >= 50000 * 3) return;
    if (!s_editorVertexOwned[index[0]] || !s_editorVertexOwned[index[1]] || !s_editorVertexOwned[index[2]]) return;
    const uint8_t texture = gevrEditorTexture(textureId);
    for (int i = 0; i < 3; ++i) {
        EditorCorner corner = {};
        const auto &v = rsp.loaded_vertices[index[i]];
        memcpy(corner.position, s_editorPositions[index[i]], sizeof(corner.position));
        if (texture) {
            corner.uv[0] = (v.u * scale[0] * (half ? .5f : 1.f) - origin[0] + (centre ? .5f : 0.f)) / size[0];
            corner.uv[1] = (v.v * scale[1] * (half ? .5f : 1.f) - origin[1] + (centre ? .5f : 0.f)) / size[1];
        }
        const auto brighten = [](uint8_t color) { return (uint8_t)(120 + color * 135 / 255); };
        corner.rgba[0] = brighten(v.color.r);
        corner.rgba[1] = brighten(v.color.g);
        corner.rgba[2] = brighten(v.color.b);
        corner.rgba[3] = alpha ? v.color.a : 255;
        corner.texture = texture;
        s_editorCorners.push_back(corner);
    }
}
