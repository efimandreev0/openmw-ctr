/* Force int32_t == int / uint32_t == unsigned int on devkitARM newlib (same width/ABI). */
#undef __INT32_TYPE__
#define __INT32_TYPE__ int
#undef __UINT32_TYPE__
#define __UINT32_TYPE__ unsigned int
#undef __INT_LEAST32_TYPE__
#define __INT_LEAST32_TYPE__ int
#undef __UINT_LEAST32_TYPE__
#define __UINT_LEAST32_TYPE__ unsigned int
