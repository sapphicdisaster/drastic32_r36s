// LGPL-2.1 License
// (C) 2025 Steward Fu <steward.fu@gmail.com>

#include <time.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include <sys/ioctl.h>
#include <json-c/json.h>
#include <alsa/output.h>
#include <alsa/input.h>
#include <alsa/conf.h>
#include <alsa/global.h>
#include <alsa/timer.h>
#include <alsa/pcm.h>
#include <linux/rtc.h>
#include <linux/soundcard.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <syslog.h>
#include <dlfcn.h>

#if defined(FXTEC_QX1000) || defined(MOTO_XT894) || defined(MOTO_XT897) || defined(UT)
#include <pulse/pulseaudio.h>
#endif

#if defined(UT)
#include "unity_fixture.h"
#endif

#include "snd.h"
#include "hook.h"
#include "common.h"

#if defined(R36S)
static void *alsa_lib = NULL;
typedef struct _snd_pcm snd_pcm_real_t;
static int (*real_snd_pcm_open)(snd_pcm_real_t **pcm, const char *name, int stream, int mode);
static int (*real_snd_pcm_hw_params_any)(snd_pcm_real_t *pcm, void *params);
static int (*real_snd_pcm_hw_params_set_access)(snd_pcm_real_t *pcm, void *params, int _access);
static int (*real_snd_pcm_hw_params_set_format)(snd_pcm_real_t *pcm, void *params, int format);
static int (*real_snd_pcm_hw_params_set_channels)(snd_pcm_real_t *pcm, void *params, unsigned int val);
static int (*real_snd_pcm_hw_params_set_rate_near)(snd_pcm_real_t *pcm, void *params, unsigned int *val, int *dir);
static int (*real_snd_pcm_hw_params)(snd_pcm_real_t *pcm, void *params);
static int (*real_snd_pcm_hw_params_free)(void *obj);
static int (*real_snd_pcm_hw_params_malloc)(void **ptr);
static int (*real_snd_pcm_prepare)(snd_pcm_real_t *pcm);
static long (*real_snd_pcm_writei)(snd_pcm_real_t *pcm, const void *buffer, unsigned long size);
static int (*real_snd_pcm_close)(snd_pcm_real_t *pcm);
static int (*real_snd_pcm_delay)(snd_pcm_real_t *pcm, snd_pcm_sframes_t *delayp);
static int (*real_snd_pcm_recover)(snd_pcm_real_t *pcm, int err, int silent);
static int (*real_snd_pcm_resume)(snd_pcm_real_t *pcm);
static int (*real_snd_pcm_wait)(snd_pcm_real_t *pcm, int timeout);
static int (*real_snd_pcm_state)(snd_pcm_real_t *pcm);
static snd_pcm_real_t *real_pcm = NULL;
#endif

#if defined(MIYOO_MINI) || defined(UT)
#include "mi_ao.h"
#include "mi_sys.h"
#include "mi_common_datatype.h"
#endif

typedef struct {
    int size;
    int rsize;
    int wsize;
    uint8_t *buf;
    pthread_mutex_t lock;
} queue_t;

#if defined(FXTEC_QX1000) || defined(MOTO_XT894) || defined(MOTO_XT897) || defined(UT)
struct mypulse_t {
    pa_threaded_mainloop *mainloop;
    pa_context *context;
    pa_mainloop_api *api;
    pa_stream *stream;
    pa_sample_spec spec;
    pa_buffer_attr attr;
} mypulse = { 0 };
#endif

#if defined(MIYOO_MINI) || defined(UT)
struct {
    MI_AO_CHN ch;
    MI_AUDIO_DEV id;
    MI_AUDIO_Attr_t sattr;
    MI_AUDIO_Attr_t gattr;
} myao = { 0 };
#endif

struct mypcm_t {
    int ready;
    int len;
    uint8_t *buf;
} mypcm = { 0 };

#if defined(TRIMUI_SMART) || defined(PANDORA) || defined(UT) || defined(TRIMUI_BRICK) || defined(R36S)
static int dsp_fd = -1;
#endif

extern nds_hook myhook;

static audio_struct *last_audio = NULL;

static int cur_vol = 100;
static pthread_t thread = { 0 };

static queue_t queue = { 0 };

void dump_audio_buffer(void)
{
    if (!last_audio) {
        printf("[DUMP] No audio struct pointer yet\n");
        return;
    }
    printf("[DUMP] Audio pointer: %p\n", last_audio);
    printf("[DUMP] idx=%d, out=%d, sys_en=%d, pause=%d\n",
        last_audio->buffer_index,
        last_audio->enable_output,
        (myhook.var.system.config.enable_sound ? *myhook.var.system.config.enable_sound : -1),
        last_audio->pause_state);
    
    uint32_t *p = (uint32_t *)last_audio->buffer;
    printf("[DUMP] buffer[0..7]: %08x %08x %08x %08x %08x %08x %08x %08x\n", 
        p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7]);
}

static int init_queue(queue_t *, size_t);
static int quit_queue(queue_t *);
static int put_queue(queue_t *, uint8_t *, size_t);
static size_t get_queue(queue_t *, uint8_t *, size_t);

static int get_available_rsize(queue_t *q)
{
    int r = 0;

    if (!q) {
        return 0;
    }

    pthread_mutex_lock(&q->lock);
    if (q->wsize >= q->rsize) {
        r = q->wsize - q->rsize;
    }
    else {
        r = q->size - q->rsize + q->wsize;
    }
    pthread_mutex_unlock(&q->lock);

    return r;
}

static int open_alsa_lib(void)
{
#if defined(R36S)
    if (alsa_lib == NULL) {
        alsa_lib = dlopen("/usr/lib/arm-linux-gnueabihf/libasound.so.2", RTLD_LAZY);
        if (alsa_lib == NULL) {
            error("failed to dlopen libasound.so.2: %s\n", dlerror());
            return -1;
        }
        trace_throttled("successfully loaded libasound.so.2\n");

        real_snd_pcm_open = dlsym(alsa_lib, "snd_pcm_open");
        real_snd_pcm_hw_params_any = dlsym(alsa_lib, "snd_pcm_hw_params_any");
        real_snd_pcm_hw_params_set_access = dlsym(alsa_lib, "snd_pcm_hw_params_set_access");
        real_snd_pcm_hw_params_set_format = dlsym(alsa_lib, "snd_pcm_hw_params_set_format");
        real_snd_pcm_hw_params_set_channels = dlsym(alsa_lib, "snd_pcm_hw_params_set_channels");
        real_snd_pcm_hw_params_set_rate_near = dlsym(alsa_lib, "snd_pcm_hw_params_set_rate_near");
        real_snd_pcm_hw_params = dlsym(alsa_lib, "snd_pcm_hw_params");
        real_snd_pcm_hw_params_free = dlsym(alsa_lib, "snd_pcm_hw_params_free");
        real_snd_pcm_hw_params_malloc = dlsym(alsa_lib, "snd_pcm_hw_params_malloc");
        real_snd_pcm_prepare = dlsym(alsa_lib, "snd_pcm_prepare");
        real_snd_pcm_writei = dlsym(alsa_lib, "snd_pcm_writei");
        real_snd_pcm_close = dlsym(alsa_lib, "snd_pcm_close");
        real_snd_pcm_delay = dlsym(alsa_lib, "snd_pcm_delay");
        real_snd_pcm_recover = dlsym(alsa_lib, "snd_pcm_recover");
        real_snd_pcm_resume = dlsym(alsa_lib, "snd_pcm_resume");
        real_snd_pcm_wait = dlsym(alsa_lib, "snd_pcm_wait");
        real_snd_pcm_state = dlsym(alsa_lib, "snd_pcm_state");
        
        if (!real_snd_pcm_open || !real_snd_pcm_writei) {
            error("failed to find critical ALSA symbols\n");
            return -1;
        }
    }
#endif
    return 0;
}

#if defined(UT) || defined(TRIMUI_SMART) || defined(PANDORA) || defined(TRIMUI_BRICK) || defined(R36S)
static int open_dsp(void)
{
#if defined(R36S)
    void *params = NULL;
    unsigned int val = 0;
    int dir = 0;

    trace_throttled("call %s() for R36S\n", __func__);

    if (open_alsa_lib() < 0) {
        return -1;
    }

    if (real_pcm) {
        real_snd_pcm_close(real_pcm);
        real_pcm = NULL;
    }

    // Use "default" to respect asound.conf/dmix
    int ret = real_snd_pcm_open(&real_pcm, "default", 0, 1); // SND_PCM_NONBLOCK
    if (ret < 0) {
        error("failed to open ALSA default device (ret=%d)\n", ret);
        return -1;
    }
    trace_throttled("successfully opened ALSA device\n");

    real_snd_pcm_hw_params_malloc(&params);
    real_snd_pcm_hw_params_any(real_pcm, params);
    real_snd_pcm_hw_params_set_access(real_pcm, params, 3); // SND_PCM_ACCESS_RW_INTERLEAVED
    real_snd_pcm_hw_params_set_format(real_pcm, params, 2); // SND_PCM_FORMAT_S16_LE
    real_snd_pcm_hw_params_set_channels(real_pcm, params, SND_CHANNELS);
    
    val = SND_FREQ;
    real_snd_pcm_hw_params_set_rate_near(real_pcm, params, &val, &dir);
    
    unsigned long buffer_size = 4096;
    unsigned long period_size = 1024;
    
    int (*real_snd_pcm_hw_params_set_buffer_size_near)(snd_pcm_real_t *, void *, unsigned long *) = dlsym(alsa_lib, "snd_pcm_hw_params_set_buffer_size_near");
    int (*real_snd_pcm_hw_params_set_period_size_near)(snd_pcm_real_t *, void *, unsigned long *, int *) = dlsym(alsa_lib, "snd_pcm_hw_params_set_period_size_near");
    
    if (real_snd_pcm_hw_params_set_buffer_size_near)
        real_snd_pcm_hw_params_set_buffer_size_near(real_pcm, params, &buffer_size);
    if (real_snd_pcm_hw_params_set_period_size_near)
        real_snd_pcm_hw_params_set_period_size_near(real_pcm, params, &period_size, &dir);

    ret = real_snd_pcm_hw_params(real_pcm, params);
    if (ret < 0) {
        error("failed to set ALSA hw params (ret=%d)\n", ret);
    }
    real_snd_pcm_hw_params_free(params);
    real_snd_pcm_prepare(real_pcm);
    
    uint8_t *silence = calloc(1, 8192 * 2 * SND_CHANNELS);
    if (silence) {
        real_snd_pcm_writei(real_pcm, silence, 8192);
        free(silence);
    }

    trace_throttled("ALSA preparation complete\n");
    return 0;
#else
    int arg = 0;

    trace_throttled("call %s()\n", __func__);

    if (dsp_fd > 0) {
        close(dsp_fd);
    }

    dsp_fd = open(DSP_DEV, O_WRONLY);
    if (dsp_fd < 0) {
        error("failed to open \"%s\" device\n", DSP_DEV);
        return -1;
    }

    arg = 16;
    ioctl(dsp_fd, SOUND_PCM_WRITE_BITS, &arg);

    arg = SND_CHANNELS;
    ioctl(dsp_fd, SOUND_PCM_WRITE_CHANNELS, &arg);

    arg = SND_FREQ;
    ioctl(dsp_fd, SOUND_PCM_WRITE_RATE, &arg);

    return 0;
#endif
}
#endif

static int init_queue(queue_t *q, size_t s)
{
    trace_throttled("call %s(q=%p, s=%ld)\n", __func__, q, s);

    if (!q) {
        error("q is null\n");
    }

    if (s == 0) {
        error("invalid size\n");
        return -1;
    }

    q->buf = (uint8_t *)malloc(s);
    q->size = s;
    q->rsize = q->wsize = 0;
    pthread_mutex_init(&q->lock, NULL);

    return 0;
}

static int quit_queue(queue_t *q)
{
    trace_throttled("call %s(q=%p)\n", __func__, q);

    if (!q) {
        return -1;
    }

    if (q->buf) {
        free(q->buf);
        q->buf = NULL;
    }
    pthread_mutex_destroy(&q->lock);

    return 0;
}

static int put_queue(queue_t *q, uint8_t *buf, size_t len)
{
    int r = 0;
    int tmp = 0;
    int avai = 0;
    int size = len;

    if (!q || !buf) {
        error("invalid parameters\n");
        return -1;
    }

    if (len == 0) {
        return 0;
    }

    pthread_mutex_lock(&q->lock);
    if (q->wsize >= q->rsize) {
        avai = q->size - q->wsize + q->rsize;
    }
    else {
        avai = q->rsize - q->wsize;
    }

    if (size > avai) {
        size = avai;
    }
    r = size;

    if (size > 0) {
        if ((q->wsize + size) > q->size) {
            tmp = q->size - q->wsize;
            size-= tmp;

            neon_memcpy(&q->buf[q->wsize], buf, tmp);
            neon_memcpy(q->buf, &buf[tmp], size);
            q->wsize = size;
        }
        else {
            neon_memcpy(&q->buf[q->wsize], buf, size);
            q->wsize += size;
        }
    }
    pthread_mutex_unlock(&q->lock);

    return r;
}

static size_t get_queue(queue_t *q, uint8_t *buf, size_t len)
{
    int r = 0;
    int tmp = 0;
    int avai = 0;
    int size = len;

    if (!q || !buf) {
        error("invalid parameters\n");
        return -1;
    }

    if (len == 0) {
        return 0;
    }

    pthread_mutex_lock(&q->lock);
    if (q->wsize >= q->rsize) {
        avai = q->wsize - q->rsize;
    }
    else {
        avai = q->size - q->rsize + q->wsize;
    }

    if (size > avai) {
        size = avai;
    }
    r = size;

    if (size > 0) {
        if ((q->rsize + size) > q->size) {
            tmp = q->size - q->rsize;
            size-= tmp;

            neon_memcpy(buf, &q->buf[q->rsize], tmp);
            neon_memcpy(&buf[tmp], q->buf, size);
            q->rsize = size;
        }
        else {
            neon_memcpy(buf, &q->buf[q->rsize], size);
            q->rsize += size;
        }
    }
    pthread_mutex_unlock(&q->lock);

    return r;
}

static void* audio_handler(void *id)
{
#if defined(MIYOO_MINI) || defined(UT)
    MI_AUDIO_Frame_t frame = { 0 };
#endif

    int r = 0;
    int idx = 0;
    int len = mypcm.len;

    trace_throttled("call %s()++\n", __func__);

#if defined(UT)
    mypcm.ready = 0;
#endif

    while (mypcm.ready) {
        int available = get_available_rsize(&queue);
        // trace_throttled("available=%d\n", available);
        if (available >= mypcm.len / 2) {
            r = get_queue(&queue, &mypcm.buf[idx], len);
            if (r > 0) {
                idx+= r;
                len-= r;
                if (len == 0) {
                    idx = 0;
                    len = mypcm.len;
#if defined(MIYOO_MINI) || defined(UT)
                    frame.eBitwidth = myao.gattr.eBitwidth;
                    frame.eSoundmode = myao.gattr.eSoundmode;
                    frame.u32Len = mypcm.len;
                    frame.apVirAddr[0] = mypcm.buf;
                    frame.apVirAddr[1] = NULL;
                    MI_AO_SendFrame(myao.id, myao.ch, &frame, 1);
#endif

#if defined(TRIMUI_SMART) || defined(PANDORA) || defined(TRIMUI_BRICK)
                    write(dsp_fd, mypcm.buf, mypcm.len);
#endif

#if defined(R36S)
                    if (real_pcm) {
                        long frames = mypcm.len / (2 * SND_CHANNELS);
                        
                        // Check state and recover from suspension
                        if (real_snd_pcm_state) {
                            int state = real_snd_pcm_state(real_pcm);
                            if (state == 7) { // SND_PCM_STATE_SUSPENDED
                                trace_throttled("ALSA suspended, attempting resume...\n");
                                if (real_snd_pcm_resume) real_snd_pcm_resume(real_pcm);
                                real_snd_pcm_prepare(real_pcm);
                            }
                        }

                        if (cur_vol < 100) {
                            int16_t *samples = (int16_t *)mypcm.buf;
                            int num_samples = mypcm.len / 2;
                            for (int i = 0; i < num_samples; i++) {
                                samples[i] = (samples[i] * cur_vol) / 100;
                            }
                        }

                        // Check for silence
                        int16_t *chk = (int16_t *)mypcm.buf;
                        int not_silent = 0;
                        for(int k=0; k<frames*SND_CHANNELS; ++k) {
                            if(chk[k] != 0) { not_silent = 1; break; }
                        }
                        if (!not_silent) {
                            trace_throttled("writing SILENCE to ALSA\n");
                        } else {
                            trace_throttled("writing AUDIO DATA to ALSA\n");
                        }
                        
                        long total_written = 0;
                        long frames_to_write = frames;
                        uint8_t *ptr = mypcm.buf;
                        int retries = 0;
                        int error_retries = 0;
                        
                        while (total_written < frames) {
                            long written = real_snd_pcm_writei(real_pcm, ptr, frames_to_write);
                            if (written < 0) {
                                if (written == -EAGAIN) {
                                    if (retries++ < 3) {
                                        // Wait up to 20ms for buffer space
                                        if (real_snd_pcm_wait) {
                                            real_snd_pcm_wait(real_pcm, 20);
                                        } else {
                                            usleep(5000);
                                        }
                                        continue;
                                    } else {
                                        trace_throttled("ALSA buffer full, dropping remaining %ld frames (frameskip)\n", frames_to_write);
                                        break; // Drop the rest
                                    }
                                }
                                
                                // Handle other errors (like -32 Broken Pipe)
                                if (error_retries++ < 10) {
                                    trace_throttled("ALSA error: %ld, attempting recovery (attempt %d)\n", written, error_retries);
                                    
                                    myhook.rcv_active = 1;
                                    snprintf(myhook.rcv_msg, sizeof(myhook.rcv_msg), "ALSA Recovering... %d/10", error_retries);
                                    
                                    if (real_snd_pcm_recover) {
                                        real_snd_pcm_recover(real_pcm, (int)written, 0);
                                    } else {
                                        real_snd_pcm_prepare(real_pcm);
                                    }
                                    usleep(5000); // Prevent tight loop
                                    continue;
                                } else {
                                    trace_throttled("ALSA error persistence, dropping frame\n");
                                    myhook.rcv_active = 1;
                                    snprintf(myhook.rcv_msg, sizeof(myhook.rcv_msg), "ALSA Failed! Dropping audio.");
                                    break;
                                }
                            }
                            
                            // Success (full or partial write)
                            if (myhook.rcv_active) {
                                myhook.rcv_active = 0;
                            }
                            total_written += written;
                            frames_to_write -= written;
                            ptr += (written * 2 * SND_CHANNELS);
                            retries = 0; // Reset retries on progress
                            error_retries = 0;
                        }
                    }
#endif

#if defined(FXTEC_QX1000) || defined(MOTO_XT894) || defined(MOTO_XT897)
                    if (mypulse.mainloop) {
                        pa_threaded_mainloop_lock(mypulse.mainloop);
                        pa_stream_write(mypulse.stream, mypcm.buf, mypcm.len, NULL, 0, PA_SEEK_RELATIVE);
                        pa_threaded_mainloop_unlock(mypulse.mainloop);
                    }
#endif
                }
            }
        }
        usleep(10);
    }

    trace_throttled("call %s()--\n", __func__);
    pthread_exit(NULL);
}

snd_pcm_sframes_t snd_pcm_avail(snd_pcm_t *pcm)
{
    if ((uintptr_t)pcm == SND_PCM_STREAM_CAPTURE) {
        trace_throttled("capture flush (use_mic=%d)\n", myhook.use_mic);
        return 0;
    }

    return 2048;
}

int snd_pcm_hw_params(snd_pcm_t *pcm, snd_pcm_hw_params_t *params)
{
    trace_throttled("call %s()\n", __func__);
    return 0;
}

int snd_pcm_hw_params_any(snd_pcm_t *pcm, snd_pcm_hw_params_t *params)
{
    trace_throttled("call %s()\n", __func__);
    return 0;
}

void snd_pcm_hw_params_free(snd_pcm_hw_params_t *obj)
{
    trace_throttled("call %s()\n", __func__);
}

int snd_pcm_hw_params_malloc(snd_pcm_hw_params_t **ptr)
{
    trace_throttled("call %s()\n", __func__);
    return 0;
}

int snd_pcm_hw_params_set_access(
    snd_pcm_t *pcm,
    snd_pcm_hw_params_t *params,
    snd_pcm_access_t _access)
{
    trace_throttled("call %s(pcm=%p, params=%p, access=%d)\n", __func__, pcm, params, _access);
    return 0;
}

int snd_pcm_hw_params_set_format(
    snd_pcm_t *pcm,
    snd_pcm_hw_params_t *params,
    snd_pcm_format_t format)
{
    trace_throttled("call %s(pcm=%p, params=%p, format=%d)\n", __func__, pcm, params, format);
    return 0;
}

int snd_pcm_hw_params_set_channels(
    snd_pcm_t *pcm,
    snd_pcm_hw_params_t *params,
    unsigned int val)
{
    trace_throttled("call %s(pcm=%p, params=%p, val=%d)\n", __func__, pcm, params, val);
    return 0;
}

int snd_pcm_hw_params_set_buffer_size_near(
    snd_pcm_t *pcm,
    snd_pcm_hw_params_t *params,
    snd_pcm_uframes_t *val)
{
    trace_throttled("call %s(pcm=%p, params=%p, val=%p)\n", __func__, pcm, params, val);
    *val = 8192;
    return 0;
}

int snd_pcm_hw_params_set_period_size_near(
    snd_pcm_t *pcm,
    snd_pcm_hw_params_t *params,
    snd_pcm_uframes_t *val,
    int *dir)
{
    trace_throttled("call %s(pcm=%p, params=%p, val=%p, dir=%p)\n", __func__, pcm, params, val, dir);

    *val = SND_PERIOD;
    return 0;
}

int snd_pcm_hw_params_set_rate_near(
    snd_pcm_t *pcm,
    snd_pcm_hw_params_t *params,
    unsigned int *val,
    int *dir)
{
    trace_throttled("call %s(pcm=%p, params=%p, val=%p, dir=%p)\n", __func__, pcm, params, val, dir);

    *val = SND_FREQ;
    return 0;
}

int snd_pcm_open(snd_pcm_t **pcm, const char *name, snd_pcm_stream_t stream, int mode)
{
    trace_throttled("call %s(pcm=%p, name=%s, stream=%d, mode=%d)\n", __func__, pcm, name, stream, mode);

    if (stream != SND_PCM_STREAM_PLAYBACK) {
        return -1;
    }

    if (pcm) {
        *pcm = (snd_pcm_t *)0x12345678;
    }
    return 0;
}

int snd_pcm_prepare(snd_pcm_t *pcm)
{
    trace_throttled("call %s(pcm=%ld)\n", __func__, (uintptr_t)pcm);
    return 0;
}

snd_pcm_sframes_t snd_pcm_readi(snd_pcm_t *pcm, void *buf, snd_pcm_uframes_t size)
{
    trace_throttled("call %s(pcm=%ld, buf=%p, size=%ld)\n", __func__, (uintptr_t)pcm, buf, size);
    return 0;
}

int snd_pcm_recover(snd_pcm_t *pcm, int err, int silent)
{
    trace_throttled("call %s(pcm=%p, err=%d, silent=%d)\n", __func__, pcm, err, silent);
    return 0;
}

int snd_pcm_delay(snd_pcm_t *pcm, snd_pcm_sframes_t *delayp)
{
    // trace_throttled("call %s()\n", __func__);
    if (delayp) {
        snd_pcm_sframes_t real_delay = 0;
#if defined(R36S)
        if (real_pcm && real_snd_pcm_delay) {
            real_snd_pcm_delay(real_pcm, &real_delay);
        }
#endif
        pthread_mutex_lock(&queue.lock);
        int bytes = (queue.wsize >= queue.rsize) ? (queue.wsize - queue.rsize) : (queue.size - queue.rsize + queue.wsize);
        pthread_mutex_unlock(&queue.lock);
        *delayp = (bytes / (2 * SND_CHANNELS)) + real_delay;

        if (*delayp > SND_FREQ) {
            trace_throttled("ALSA delay too high (%ld), capping to 1s\n", (long)*delayp);
            *delayp = SND_FREQ;
        }
    }
    return 0;
}

snd_pcm_sframes_t snd_pcm_avail_update(snd_pcm_t *pcm)
{
    // trace_throttled("call %s()\n", __func__);
    return snd_pcm_avail(pcm);
}

static void prehook_audio_synchronous_update(audio_struct *audio, uint32_t non_blocking, uint32_t audio_capture)
{
    last_audio = audio;
    // trace_throttled("call %s(audio=%p, non_blocking=%d, audio_capture=%d)\n", __func__, audio, non_blocking, audio_capture);
    if (audio) {
        trace_throttled("audio: idx=%d, out=%d, sys_en=%d, pause=%d\n", 
            audio->buffer_index, 
            audio->enable_output, 
            (myhook.var.system.config.enable_sound ? *myhook.var.system.config.enable_sound : -1),
            audio->pause_state);

        if (audio->buffer_index > 0) {
            put_queue(&queue, (uint8_t*)audio->buffer, audio->buffer_index * 2);
            audio->buffer_index = 0;
        }
    }
}

static void prehook_adpcm_decode_block(spu_channel_struct *channel)
{
    uint32_t uVar1 = 0;
    uint32_t uVar2 = 0;
    uint32_t uVar3 = 0;
    uint32_t uVar4 = 0;
    uint32_t sample_delta = 0;
    uint32_t current_index = 0;
    uint32_t adpcm_data_x8 = 0;
    uint32_t adpcm_cache_block_offset = 0;
    uint32_t adpcm_step = 0;
    uint32_t uVar5 = 0;
    int32_t sample = 0;
    int16_t *psVar6 = NULL;
    int16_t *psVar7 = NULL;
    int16_t *adpcm_step_table = NULL;
    int8_t *adpcm_index_step_table = NULL;

    // trace_throttled("call %s(channel=%p)\n", __func__, channel);

    adpcm_step_table = (int16_t *)myhook.var.adpcm.step_table;
    adpcm_index_step_table = (int8_t *)myhook.var.adpcm.index_step_table;
    do {
        if (!channel) {
            error("invalid input\n");
            break;
        }

        uVar3 = channel->adpcm_cache_block_offset;
        uVar1 = (uint32_t)(channel->adpcm_current_index);
        sample_delta = (uint32_t)(channel->adpcm_sample);
        uVar2 = *((uint32_t *)(channel->samples + (uVar3 >> 1)));
        channel->adpcm_cache_block_offset = uVar3 + 8;
        psVar7 = channel->adpcm_sample_cache + (uVar3 & 0x3f);
        do {
            uVar5 = (uint32_t)adpcm_step_table[uVar1];
            uVar4 = uVar5 >> 3;
            if ((uVar2 & 1) != 0) {
                uVar4 = uVar4 + (uVar5 >> 2);
            }
            if ((uVar2 & 2) != 0) {
                uVar4 = uVar4 + (uVar5 >> 1);
            }
            if ((uVar2 & 4) != 0) {
                uVar4 = uVar4 + uVar5;
            }
            if ((uVar2 & 8) == 0) {
                sample_delta = sample_delta - uVar4;
                if ((int)sample_delta < -0x7fff) {
                    sample_delta = 0xffff8001;
                }
            }
            else {
                sample_delta = sample_delta + uVar4;
                if (0x7ffe < (int)sample_delta) {
                    sample_delta = 0x7fff;
                }
            }
            uVar1 = uVar1 + (int)(adpcm_index_step_table[uVar2 & 7]);
            if (0x58 < uVar1) {
                if ((int)uVar1 < 0) {
                    uVar1 = 0;
                }
                else {
                    uVar1 = 0x58;
                }
            }
            uVar2 = uVar2 >> 4;
            psVar6 = psVar7 + 1;
            *psVar7 = (int16_t)sample_delta;
            psVar7 = psVar6;
        } while (channel->adpcm_sample_cache + (uVar3 & 0x3f) + 8 != psVar6);
        channel->adpcm_sample = (int16_t)sample_delta;
        channel->adpcm_current_index = (uint8_t)uVar1;
    } while(0);
}

static void prehook_audio_buffer_force_feed(audio_struct *audio)
{
    // trace_throttled("call %s()\n", __func__);
    if (audio && (audio->buffer_index > 0)) {
        put_queue(&queue, (uint8_t*)audio->buffer, audio->buffer_index * 2);
        audio->buffer_index = 0;
    }
}

static void init_snd_hook_table(void)
{
    // R36S / 32-bit addresses
    myhook.fun.spu_adpcm_decode_block = (void *)0x0808d268;
    myhook.fun.audio_synchronous_update = (void *)0x080aa7c0;
    myhook.fun.audio_buffer_force_feed = (void *)0x080aa760;
    myhook.var.system.config.enable_sound = (uint32_t *)0x084797d0;
    myhook.var.fast_forward = (uint32_t *)0x084797cc;

    // ADPCM tables
    myhook.var.adpcm.step_table = (uint32_t *)0x0815a600;
    myhook.var.adpcm.index_step_table = (uint32_t *)0x0815a6b8;
}

int snd_pcm_start(snd_pcm_t *pcm)
{
    trace_throttled("call %s(pcm=%ld)\n", __func__, (uintptr_t)pcm);

    init_snd_hook_table();

    mypcm.len = SND_PERIOD * 2 * SND_CHANNELS;
    mypcm.buf = (uint8_t *)malloc(mypcm.len);
    if (mypcm.buf == NULL) {
        error("failed to allocate audio buffer\n");
        return -1;
    }

    init_queue(&queue, (size_t)DEF_QUEUE_SIZE);
    if (queue.buf == NULL) {
        error("failed to allocate circle queue\n");
        return -1;
    }
    memset(queue.buf, 0, DEF_QUEUE_SIZE);

#if defined(MIYOO_MINI) || defined(UT)
    MI_AUDIO_Attr_t stAoSetAttr;
    MI_SYS_ChnOutputPort_t stAoChn0OutputPort0;

    myao.id = 0;
    myao.ch = 0;
    myao.sattr.eBitwidth = E_MI_AUDIO_BITWIDTH_16;
    myao.sattr.eSamplerate = E_MI_AUDIO_SAMPLE_RATE_44100;
    myao.sattr.eSoundmode = E_MI_AUDIO_SOUND_MODE_STEREO;
    myao.sattr.eWorkmode = E_MI_AUDIO_MODE_I2S_MASTER;
    myao.sattr.u32ChnCnt = 2;
    myao.sattr.u32FrmNumPerBuf = 128;
    myao.sattr.u32PtNumPerFrm = SND_PERIOD;

    MI_AO_SetPubAttr(myao.id, &myao.sattr);
    MI_AO_GetPubAttr(myao.id, &myao.gattr);
    MI_AO_Enable(myao.id);
    MI_AO_EnableChn(myao.id, myao.ch);

    stAoChn0OutputPort0.eModId = E_MI_MODULE_ID_AO;
    stAoChn0OutputPort0.u32DevId = myao.id;
    stAoChn0OutputPort0.u32ChnId = myao.ch;
    stAoChn0OutputPort0.u32PortId = 0;
    MI_SYS_SetChnOutputPortDepth(&stAoChn0OutputPort0, 12, 13);
#endif

#if defined(TRIMUI_BRICK) || defined(TRIMUI_SMART) || defined(PANDORA) || defined(R36S)
    open_dsp();
#endif

#if defined(FXTEC_QX1000) || defined(MOTO_XT894) || defined(MOTO_XT897)
    mypulse.mainloop = pa_threaded_mainloop_new();
    mypulse.api = pa_threaded_mainloop_get_api(mypulse.mainloop);
    mypulse.context = pa_context_new(mypulse.api, "DraStic");
    pa_context_connect(mypulse.context, NULL, 0, NULL);
    pa_threaded_mainloop_start(mypulse.mainloop);

    while (pa_context_get_state(mypulse.context) != PA_CONTEXT_READY) {
        pa_threaded_mainloop_wait(mypulse.mainloop);
    }

    mypulse.spec.format = PA_SAMPLE_S16LE;
    mypulse.spec.channels = SND_CHANNELS;
    mypulse.spec.rate = SND_FREQ;
    mypulse.stream = pa_stream_new(mypulse.context, "NDS", &mypulse.spec, NULL);
    pa_stream_connect_playback(mypulse.stream, NULL, NULL, 0, NULL, NULL);
#endif

    add_prehook((void *)myhook.fun.spu_adpcm_decode_block, prehook_adpcm_decode_block, NULL);
    add_prehook((void *)myhook.fun.audio_synchronous_update, prehook_audio_synchronous_update, NULL);
    add_prehook((void *)myhook.fun.audio_buffer_force_feed, prehook_audio_buffer_force_feed, NULL);

#if USE_CIRCLE_QUEUE
    mypcm.ready = 1;
    pthread_create(&thread, NULL, audio_handler, (void *)NULL);
#endif

    return 0;
}

int snd_pcm_close(snd_pcm_t *pcm)
{
    trace_throttled("call %s(pcm=%p)\n", __func__, pcm);

#if USE_CIRCLE_QUEUE
    mypcm.ready = 0;
    pthread_detach(thread);
    quit_queue(&queue);
#endif

#if defined(R36S)
    if (real_pcm) {
        real_snd_pcm_close(real_pcm);
        real_pcm = NULL;
    }
    if (alsa_lib) {
        dlclose(alsa_lib);
        alsa_lib = NULL;
    }
#endif

    if (dsp_fd > 0) {
        close(dsp_fd);
        dsp_fd = -1;
    }

    return 0;
}

int snd_pcm_sw_params(snd_pcm_t *pcm, snd_pcm_sw_params_t *params)
{
    trace_throttled("call %s()\n", __func__);
    return 0;
}

int snd_pcm_sw_params_current(snd_pcm_t *pcm, snd_pcm_sw_params_t *params)
{
    trace_throttled("call %s()\n", __func__);
    return 0;
}

void snd_pcm_sw_params_free(snd_pcm_sw_params_t *obj)
{
    trace_throttled("call %s()\n", __func__);
}

int snd_pcm_sw_params_malloc(snd_pcm_sw_params_t **ptr)
{
    trace_throttled("call %s()\n", __func__);
    return 0;
}

snd_pcm_sframes_t snd_pcm_writei(snd_pcm_t *pcm, const void *buf, snd_pcm_uframes_t size)
{
    trace_throttled("call %s(pcm=%p, size=%ld)\n", __func__, pcm, size);
    if ((size > 1) && (size != mypcm.len)) {
        put_queue(&queue, (uint8_t*)buf, size * 2 * SND_CHANNELS);
    }
    return size;
}