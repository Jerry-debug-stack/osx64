#include "uprintf.h"
#include "sysapi.h"
#include <mmintrin.h>
#include <xmmintrin.h>

short mmx_result[4] __attribute__((aligned(8)));

int main() {
    printf("Testing x87 FPU, MMX, and SSE support...\n");

    // ----- x87 浮点测试 -----
    double a = 3.14159, b = 2.71828;
    double c = a * b;
    
    printf("x87: %f * %f = %f (expected 8.5397)\n", a, b, c);

    // ----- SSE 单精度向量测试 -----
    __m128 va = _mm_set_ps(1.0f, 2.0f, 3.0f, 4.0f);
    __m128 vb = _mm_set_ps(4.0f, 3.0f, 2.0f, 1.0f);
    __m128 vc = _mm_add_ps(va, vb);
    float res[4];
    _mm_storeu_ps(res, vc);  // 不对齐存储，方便打印
    printf("SSE: [4.0,3.0,2.0,1.0] + [1.0,2.0,3.0,4.0] = [%f,%f,%f,%f]\n",
           res[3], res[2], res[1], res[0]);

    // ----- MMX 打包整数加法测试 -----
    __m64 ma = _mm_set_pi16(1, 2, 3, 4);
    __m64 mb = _mm_set_pi16(5, 6, 7, 8);
    __m64 mc = _mm_add_pi16(ma, mb);

    // 直接将 MMX 寄存器内容存储到对齐内存（赋值即可）
    *(__m64*)mmx_result = mc;

    printf("MMX: [1,2,3,4] + [5,6,7,8] = [%d,%d,%d,%d]\n",
           mmx_result[0], mmx_result[1], mmx_result[2], mmx_result[3]);

    _mm_empty();  // 清空 MMX 状态，允许后续使用 x87

    exit(0);
}
