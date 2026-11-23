/* Offline export of the actual desktop actor pose for the local loadout editor. */
#include "gevr_tactical_editor_capture.h"

PropRecord *g_gevrEditorPreviewActor;
float g_gevrEditorPreviewYaw;

static Mtxf gevrEditorViewToPreview(const ChrRecord *chr)
{
    Mtxf world, preview;
    matrix_4x4_invert_affine(camGetWorldToScreenMtxf(), &world);
    const float s = sinf(g_gevrEditorPreviewYaw) * .01f;
    const float c = cosf(g_gevrEditorPreviewYaw) * .01f;
    const coord3d pos = chr->prop->pos;
    const Mtxf local = {.m = {{c,0,s,0},{0,.01f,0,0},{-s,0,c,0},
        {-c*pos.x+s*pos.z,-.01f*pos.y,-s*pos.x-c*pos.z,1}}};
    matrix_4x4_multiply_homogeneous((Mtxf *)&local, &world, &preview);
    return preview;
}

static void gevrEditorWriteFit(FILE *file, const char *name, Mtxf *viewToModel, Mtxf *mount)
{
    Mtxf transform;
    matrix_4x4_multiply_homogeneous(viewToModel, mount, &transform);
    fprintf(file, "%s", name);
    for (int row = 0; row < 4; ++row)
        for (int axis = 0; axis < 4; ++axis)
            fprintf(file, " %.8g", transform.m[row][axis] * (row < 3 && axis < 3 ? 1000.f : 1.f));
    fputc('\n', file);
}

static int gevrEditorCaptureActor(ChrRecord *chr, int withalpha)
{
    static int captured;
    if (captured || withalpha || chr->prop != g_gevrEditorPreviewActor) return 0;
    const Mtxf viewToModel = gevrEditorViewToPreview(chr);
    Mtxf local = viewToModel;
    Model *model = chr->model;
    gevrEditorCaptureSetTransform(viewToModel.m, model->render_pos, model->obj->numMatrices);
    ModelNode *chest = gevrEnemyRigHitbox(model->obj->RootNode, HIT_CHEST);
    const int chestIndex = modelFindNodeMtxIndex(chest, 0);
    FILE *file = fopen("npc-preview-fit.txt", "w");
    if (file) {
        if (chest && chestIndex >= 0 && chestIndex < model->obj->numMatrices) {
            Mtxf fit, vest;
            Mtxf *base = &model->render_pos[chestIndex].pos;
            gevrEnemyVestFit(&chest->Data->BoundingBox.Bounds, &fit);
            matrix_4x4_multiply_homogeneous(base, &fit, &vest);
            gevrEditorWriteFit(file, "vest", &local, &vest);
            for (int side = 0; side < 2; ++side) {
                Mtxf pose;
                if (gevrEnemyVestArmMount(model, chestIndex, side, &pose))
                    gevrEditorWriteFit(file, side == 0 ? "left" : "right", &local, &pose);
            }
        }
        const PropRecord *hat = chr->handle_positiondata_hat;
        if (hat && hat->obj->model && hat->obj->model->render_pos)
            gevrEditorWriteFit(file, "helmet", &local, &hat->obj->model->render_pos[0].pos);
        fclose(file);
    }
    captured = 1;
    return 1;
}
