#ifndef GEVR_TACTICAL_EDITOR_CAPTURE_H
#define GEVR_TACTICAL_EDITOR_CAPTURE_H

#define GEVR_EDITOR_CAPTURE_BEGIN 0x4e500001u
#define GEVR_EDITOR_CAPTURE_END   0x4e500002u
#ifdef __cplusplus
extern "C" {
#endif
void gevrEditorCaptureSetTransform(const float viewToModel[4][4], const void *bones, int count);
#ifdef __cplusplus
}
#endif
#endif
