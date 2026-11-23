#include "gevr_tactical_support.h"
#include "gevr_tactical_scale.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
static int near(float a, float b) { return fabsf(a-b) < .0001f; }
int main(void)
{
    float h[4][3] = {{1,0,0},{0,1,0},{0,0,1},{3,4,5}};
    float g[4][3] = {{2,0,0},{0,2,0},{0,0,2},{4,6,8}};
    float rel[4][3], restored[4][3];
    gevrSupportCapture(h,g,rel);
    gevrSupportRestore(h,rel,restored);
    for (int r=0;r<4;r++) for(int c=0;c<3;c++) assert(near(g[r][c],restored[r][c]));
    /* Move and rotate the support hand by 90 degrees: the weapon follows it,
     * while the other controller is absent from the calculation. */
    float turned[4][3]={{0,1,0},{-1,0,0},{0,0,1},{10,20,30}};
    gevrSupportRestore(turned,rel,restored);
    assert(near(restored[3][0],8) && near(restored[3][1],21) && near(restored[3][2],33));
    assert(near(restored[0][0],0) && near(restored[0][1],2));
    float back[3]={0,0,1},up[3]={0,1,0},aim[3];
    gevrTacticalAkSightDirection(back,up,aim);
    assert(near(aim[1],-.020f) && near(aim[2],-1));
    puts("AK support frame follows the forehand and sight correction points down");
    return 0;
}
