/*
 * Stub av_tx_init / av_tx_uninit to prevent 12MB of unused FFT tables
 * from being linked into the Vita executable.
 */
#if defined(__vita__) || defined(__3DS__)

#include <stddef.h>
#include <stdint.h>

typedef struct AVTXContext AVTXContext;

int av_tx_init(AVTXContext** ctx, void** tx, int type, int inv, int len, const void* scale, uint64_t flags)
{
    (void)ctx; (void)tx; (void)type; (void)inv; (void)len; (void)scale; (void)flags;
    if (ctx)
        *ctx = NULL;
    return -1;
}

void av_tx_uninit(AVTXContext** ctx)
{
    (void)ctx;
}

/*
 * Stub ff_tx_init_subtx to break the dependency chain from tx.o into
 * tx_float.o / tx_double.o / tx_int32.o which contain the 16MB FFT tables.
 */
typedef int FFTXCodeletOptions;

int ff_tx_init_subtx(AVTXContext* s, int type, uint64_t flags,
                     FFTXCodeletOptions* opts, int len, int inv, const void* scale)
{
    (void)s; (void)type; (void)flags; (void)opts; (void)len; (void)inv; (void)scale;
    return -1;
}

#endif /* __vita__ */
