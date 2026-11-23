#include "../vr/gevr_mirror_viewport.h"
#include <assert.h>
#include <stdio.h>
int main(void) {
    GevrMirrorViewport v = gevrMirrorViewport(1518, 1482, 3440, 1440, 1);
    assert(v.x == 0 && v.w == 3440 && v.h > 1440 && v.y < 0);
    assert((double)v.w / v.h > 1.023 && (double)v.w / v.h < 1.025);
    v = gevrMirrorViewport(1920, 1080, 1080, 1920, 1);
    assert(v.y == 0 && v.h == 1920 && v.w > 1080 && v.x < 0);
    v = gevrMirrorViewport(1518, 1482, 3440, 1440, 0);
    assert(v.x == 0 && v.y == 0 && v.w == 3440 && v.h == 1440);
    puts("mirror viewport: ultrawide and portrait fill without stretching; ordinary view unchanged");
    return 0;
}
