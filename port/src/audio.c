#include <PR/ultratypes.h>
#include <stdio.h>
#include <SDL.h>
#include "platform.h"
#include "config.h"
#include "audio.h"
#include "system.h"
#include "net_voice.h"
#include "net_core.h"
#include <string.h>
#include <stdlib.h>

static SDL_AudioDeviceID dev;
static const s16 *nextBuf;
static u32 nextSize = 0;

static s32 bufferSize = 512;
static s32 queueLimit = 8192;
#ifdef GEVR
static s32 sAudioPaused;
static u32 sAudioRefills;
/*
 * #229/#226: sound going bit-crushed with an echo, until a restart, on Quest,
 * in Perfect Dark VR too (its #139), which shares this output code. There
 * the Quest's own screen recording captured the game's sound clean while the
 * headset played it crunchy, and ending a recording (which rebuilds Android's
 * audio output) cured it: the fault is past the game, in Android resampling
 * the unusual 22050 Hz stream. So the device is opened at the Quest's own
 * 48000 Hz and the game's 22050 Hz mix is resampled here (SDL_AudioStream),
 * leaving Android nothing to convert; audioReset reopens the stream, as
 * ending a recording did. Audio.OutputRate 22050 restores the old path.
 */
static s32 outputRate = 48000;
static s32 sDeviceRate = 22050;
static SDL_AudioStream *sConvert;
static s32 sAudioStarted;   /* audioInit succeeded: Reset sound may reopen a closed device */

enum { TACT_AUDIO_COUNT = TACT_AUDIO_SKORPION_BASE + 4 };
typedef struct TacticalAudioSample { s16 *pcm; u32 samples; } TacticalAudioSample;
typedef struct TacticalAudioVoice { s32 sample; u32 cursor; } TacticalAudioVoice;
static TacticalAudioSample sTacticalSamples[TACT_AUDIO_COUNT];
static TacticalAudioVoice sTacticalVoices[8];
static SDL_SpinLock sTacticalAudioLock;

static void audioTacticalLoad(void)
{
    static const char *paths[TACT_AUDIO_COUNT] = {
        "mods/tactical/sounds/aks74un-magout.wav", "mods/tactical/sounds/aks74un-magin.wav",
        "mods/tactical/sounds/aks74un-rack.wav", "mods/tactical/sounds/aks74un-magrelease.wav",
        "mods/tactical/sounds/ppk-magout.wav", "mods/tactical/sounds/ppk-magin.wav",
        "mods/tactical/sounds/ppk-rack.wav", "mods/tactical/sounds/ppk-magrelease.wav",
        "mods/tactical/sounds/scorpio61-magout.wav", "mods/tactical/sounds/scorpio61-magin.wav",
        "mods/tactical/sounds/scorpio61-rack.wav", "mods/tactical/sounds/scorpio61-magrelease.wav"
    };
    if (!sysArgCheck("--tactical-physical")) return;
    const int count = sysArgCheck("--tactical-skorpion") ? TACT_AUDIO_COUNT : TACT_AUDIO_SKORPION_BASE;
    for (int i = 0; i < count; i++) {
        SDL_AudioSpec spec; SDL_AudioCVT cvt; Uint8 *wav = NULL; Uint32 length = 0;
        if (sTacticalSamples[i].pcm) continue;
        if (!SDL_LoadWAV(paths[i], &spec, &wav, &length)) {
            sysLogPrintf(LOG_WARNING, "tactical audio: cannot load %s: %s", paths[i], SDL_GetError());
            continue;
        }
        if (SDL_BuildAudioCVT(&cvt, spec.format, spec.channels, spec.freq, AUDIO_S16SYS, 2, 22050) < 0) {
            SDL_FreeWAV(wav); continue;
        }
        cvt.len = (int)length;
        cvt.buf = SDL_malloc((size_t)length * cvt.len_mult);
        if (!cvt.buf) { SDL_FreeWAV(wav); continue; }
        memcpy(cvt.buf, wav, length); SDL_FreeWAV(wav);
        if (SDL_ConvertAudio(&cvt) < 0) { SDL_free(cvt.buf); continue; }
        sTacticalSamples[i].pcm = (s16 *)cvt.buf;
        sTacticalSamples[i].samples = (u32)cvt.len_cvt / sizeof(s16);
        sysLogPrintf(LOG_NOTE, "tactical audio: loaded sound %d (%u samples)", i, sTacticalSamples[i].samples);
    }
}

void audioTacticalPlay(int sound)
{
    int slot = -1;
    if (sound < 0 || sound >= TACT_AUDIO_COUNT || !sTacticalSamples[sound].pcm) return;
    SDL_AtomicLock(&sTacticalAudioLock);
    for (int i = 0; i < 8; i++) if (!sTacticalVoices[i].sample) { slot = i; break; }
    if (slot < 0) slot = 0;
    sTacticalVoices[slot].sample = sound + 1; sTacticalVoices[slot].cursor = 0;
    SDL_AtomicUnlock(&sTacticalAudioLock);
    sysLogPrintf(LOG_NOTE, "tactical audio: play %d", sound);
}

static void audioTacticalMix(s16 *pcm, u32 samples)
{
    unsigned began = 0;
    SDL_AtomicLock(&sTacticalAudioLock);
    for (int v = 0; v < 8; v++) if (sTacticalVoices[v].sample) {
        TacticalAudioVoice *voice = &sTacticalVoices[v];
        TacticalAudioSample *sample = &sTacticalSamples[voice->sample - 1];
        if (!voice->cursor) began |= 1u << (voice->sample - 1);
        for (u32 i = 0; i < samples && voice->cursor < sample->samples; i++, voice->cursor++) {
            /* Original AK handling clips peak around 9% of full scale. */
            s32 x = pcm[i] + (s32)sample->pcm[voice->cursor] * (voice->sample <= 4 ? 5 : 1);
            pcm[i] = x > 32767 ? 32767 : x < -32768 ? -32768 : (s16)x;
        }
        if (voice->cursor >= sample->samples) voice->sample = 0;
    }
    SDL_AtomicUnlock(&sTacticalAudioLock);
    if (began && sysArgCheck("--input-script")) sysLogPrintf(LOG_NOTE, "tactical audio: mixed sound mask %u", began);
}
#endif

static s32 audioOpen(void)
{
	SDL_AudioSpec want, have;
	SDL_zero(want);
#ifdef GEVR
	want.freq = outputRate > 0 ? outputRate : 22050; /* GE mixes at 22050 (OUTPUT_RATE) */
#else
	want.freq = 22020;
#endif
	want.format = AUDIO_S16SYS;
	want.channels = 2;
	want.samples = bufferSize;
	want.callback = NULL;

	dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
	if (dev == 0) {
		sysLogPrintf(LOG_ERROR, "SDL_OpenAudio error: %s", SDL_GetError());
		return -1;
	}
#ifdef GEVR
	sDeviceRate = have.freq;
	if (sConvert) SDL_FreeAudioStream(sConvert);
	sConvert = have.freq != 22050 ? SDL_NewAudioStream(AUDIO_S16SYS, 2, 22050, AUDIO_S16SYS, 2, have.freq) : NULL;
	if (have.freq != 22050 && !sConvert) {
		/* the device holds its rate (no allowed changes): the raw 22050 Hz
		 * mix on it would play ~2.2x fast, so open it at 22050 instead */
		sysLogPrintf(LOG_ERROR, "audio: no 22050 to %d Hz converter: %s; reopening at 22050 Hz", have.freq, SDL_GetError());
		SDL_CloseAudioDevice(dev);
		dev = 0;
		if (want.freq == 22050) {
			return -1;
		}
		outputRate = 22050;
		return audioOpen();
	}
	sysLogPrintf(LOG_NOTE, "audio: %s at %d Hz, the game's 22050 Hz mix %s", SDL_GetCurrentAudioDriver(),
	             have.freq, sConvert ? "resampled here" : "as is");
#endif
	return 0;
}

s32 audioInit(void)
{
	if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
		sysLogPrintf(LOG_ERROR, "SDL audio init error: %s", SDL_GetError());
		return -1;
	}

	nextBuf = NULL;

	if (audioOpen() != 0) {
		return -1;
	}

#ifdef GEVR
	sAudioPaused = 0;
	sAudioStarted = 1;
	audioTacticalLoad();
#endif
	SDL_PauseAudioDevice(dev, 0);

	return 0;
}

#ifdef GEVR
/* The game's 22050 Hz stereo, to the device at its own rate. */
static void audioQueue(const s16 *buf, u32 bytes)
{
	if (!sConvert) {
		if (SDL_QueueAudio(dev, buf, bytes) != 0) {
			sysLogPrintf(LOG_ERROR, "SDL_QueueAudio: %s", SDL_GetError());
		}
		return;
	}
	if (SDL_AudioStreamPut(sConvert, buf, (int)bytes) != 0) {
		sysLogPrintf(LOG_ERROR, "SDL_AudioStreamPut: %s", SDL_GetError());
		return;
	}
	static s16 out[16384];
	int got;
	while ((got = SDL_AudioStreamGet(sConvert, out, sizeof(out))) > 0) {
		if (SDL_QueueAudio(dev, out, (Uint32)got) != 0) {
			sysLogPrintf(LOG_ERROR, "SDL_QueueAudio: %s", SDL_GetError());
			return;
		}
	}
}

/* "Reset sound" (the hold-Menu panel): a new output stream, as ending a Quest
 * recording gave Perfect Dark VR's players (#229). */
void audioReset(void)
{
	if (!sAudioStarted) {
		return;
	}
	if (dev) {   /* closed already when a reset before this one failed to reopen it */
		SDL_ClearQueuedAudio(dev);
		SDL_CloseAudioDevice(dev);
		dev = 0;
	}
	nextBuf = NULL;
	nextSize = 0;
	if (audioOpen() != 0) {
		/* Android can refuse the output for a moment (a recording ending):
		 * the old 22050 Hz path rather than silence; pressing again retries */
		s32 rate = outputRate;
		outputRate = 22050;
		if (rate == 22050 || audioOpen() != 0) {
			outputRate = rate;
			sysLogPrintf(LOG_ERROR, "audio: reset could not reopen the output; press Reset sound again");
			return;
		}
	}
	if (!sAudioPaused) {
		SDL_PauseAudioDevice(dev, 0);
	}
	sysLogPrintf(LOG_NOTE, "audio: reset (the output stream was reopened)");
}
#endif

#ifdef GEVR
void audioPause(void)
{
    netVoicePause();
	if (!dev) {
		return;
	}
	sAudioPaused = 1;
	nextBuf = NULL;
	nextSize = 0;
	SDL_ClearQueuedAudio(dev);
	if (sConvert) SDL_AudioStreamClear(sConvert);
	SDL_PauseAudioDevice(dev, 1);
	sysLogPrintf(LOG_NOTE, "audio: paused (queue cleared)");
}

void audioResume(void)
{
    netVoiceResume();
	if (!dev) {
		return;
	}
	sAudioPaused = 0;
	SDL_PauseAudioDevice(dev, 0);
	sysLogPrintf(LOG_NOTE, "audio: resumed");
}

void audioShutdown(void)
{
    netVoicePause();
    netVoiceReset();
	if (!dev) {
		return;
	}
	sAudioPaused = 1;
	nextBuf = NULL;
	nextSize = 0;
	SDL_ClearQueuedAudio(dev);
	SDL_PauseAudioDevice(dev, 1);
	SDL_CloseAudioDevice(dev);
	dev = 0;
	if (sConvert) SDL_FreeAudioStream(sConvert);
	sConvert = NULL;
	sysLogPrintf(LOG_NOTE, "audio: shutdown");
}

s32 audioIsPaused(void)
{
	return sAudioPaused;
}

s32 audioRequestQueueRefill(s32 targetFrames)
{
	if (sAudioPaused || !dev || audioGetSamplesBuffered() >= SDL_min(targetFrames, queueLimit)) {
		return 0;
	}
	sAudioRefills++;
	return 1;
}
#endif

s32 audioGetBytesBuffered(void)
{
#ifdef GEVR
	if (!dev) {
		return 0;
	}
#endif
#ifdef GEVR
	/* in the game's 22050 Hz stereo frames, which it sizes its audio frames by (osAiGetLength) */
	if (sDeviceRate != 22050) {
		const u64 frames = SDL_GetQueuedAudioSize(dev) / 4;
		return (s32)(frames * 22050 / (u64)sDeviceRate) * 4;
	}
#endif
	return SDL_GetQueuedAudioSize(dev);
}

s32 audioGetSamplesBuffered(void)
{
	return audioGetBytesBuffered() / 4;
}

void audioSetNextBuffer(const s16 *buf, u32 len)
{
	nextBuf = buf;
	nextSize = len;
}

void audioEndFrame(void)
{
#ifdef GEVR
	if (sAudioPaused || !dev) {
		nextBuf = NULL;
		nextSize = 0;
		return;
	}
#endif
	if (nextBuf && nextSize) {
#ifdef GEVR
        static u64 lastSubmit, lastReport;
        static u32 empty, dropped, minQueued = ~0u, maxQueued, maxGap;
        u64 now = sysGetMicroseconds();
        u32 queued = audioGetSamplesBuffered();
        u32 gap = lastSubmit ? (u32)(now - lastSubmit) : 0;
        if (lastSubmit && !queued) empty++;
        if (queued >= (u32)queueLimit) dropped++;
        if (queued < minQueued) minQueued = queued;
        if (queued > maxQueued) maxQueued = queued;
        if (gap > maxGap) maxGap = gap;
        lastSubmit = now;
        if (!lastReport) lastReport = now;
        if (now - lastReport >= 2000000) {
            extern u32 g_gevrMixClips;   /* mixer.c: dry-mix samples clipped */
            /* synthesizer.c: the update pool and voices (#229) */
            extern void gevrSynStats(u32 *, u32 *, u32 *, u32 *, u32 *, u32 *);
            u32 updFree, updLow, updFailed, voices, orphans, longest;
            gevrSynStats(&updFree, &updLow, &updFailed, &voices, &orphans, &longest);
            sysLogPrintf(LOG_NOTE, "audio queue: frames=%u..%u empty=%u dropped=%u maxgap=%u us clipped=%u refills=%u"
                         " updates=%u free (low %u, refused %u) voices=%u orphans=%u longest queue=%u",
                         minQueued, maxQueued, empty, dropped, maxGap, g_gevrMixClips, sAudioRefills,
                         updFree, updLow, updFailed, voices, orphans, longest);
            g_gevrMixClips = 0;
            sAudioRefills = 0;
            lastReport = now;
            empty = dropped = maxQueued = maxGap = 0;
            minQueued = ~0u;
        }
#endif
		if (audioGetSamplesBuffered() < queueLimit) {
            static s16 mixed[32768];
            const s16 *out = nextBuf;
            if (nextSize <= sizeof(mixed) && nextSize % (sizeof(s16) * 2) == 0) {
                memcpy(mixed, nextBuf, nextSize);
                netVoiceMix(mixed, nextSize / (sizeof(s16) * 2));
#ifdef GEVR
                audioTacticalMix(mixed, nextSize / sizeof(s16));
#endif
                out = mixed;
            }
#ifdef GEVR
            audioQueue(out, nextSize);
#else
			if (SDL_QueueAudio(dev, out, nextSize) != 0) {
                sysLogPrintf(LOG_ERROR, "SDL_QueueAudio: %s", SDL_GetError());
            }
#endif
#ifdef GEVR
            static s32 reportedPcm;
            if (!reportedPcm) {
                for (u32 i = 0; i < nextSize / sizeof(s16); i++) {
                    if (nextBuf[i] != 0) {
                        sysLogPrintf(LOG_NOTE, "audio: nonzero stereo PCM submitted to SDL at 22050 Hz");
                        reportedPcm = 1;
                        break;
                    }
                }
            }
#endif
		}
		nextBuf = NULL;
		nextSize = 0;
	}
}

/* The launcher has no GoldenEye PCM pump. Keep the same output device fed with
 * small voice-only buffers while players talk in the lobby. */
void audioVoiceIdleTick(void)
{
    if (!dev || sAudioPaused || netGetState() == NET_STATE_INGAME ||
        netGetState() == NET_STATE_OFFLINE || netGetState() == NET_STATE_CONNECTING)
        return;
    if ((u32)audioGetBytesBuffered() >= 441u * sizeof(s16) * 2 * 3) return;
    s16 voice[441 * 2] = {0};
    netVoiceMix(voice, 441);
    audioQueue(voice, sizeof(voice));
}

PD_CONSTRUCTOR static void audioConfigInit(void)
{
	configRegisterInt("Audio.BufferSize", &bufferSize, 0, 1 * 1024 * 1024);
	configRegisterInt("Audio.QueueLimit", &queueLimit, 0, 1 * 1024 * 1024);
#ifdef GEVR
	configRegisterInt("Audio.OutputRate", &outputRate, 0, 192000);
#endif
}
