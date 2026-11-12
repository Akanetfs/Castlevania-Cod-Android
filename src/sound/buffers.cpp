// IDirectSoundBuffer
#include <cmath>

#include "sound/sound.h"

using namespace kernel;
using namespace sound;

// DSBUFFERDESC: size, flags, bytes, format, mix bins
constexpr uint32_t kBufferDescFlags = 4;
constexpr uint32_t kBufferDescBytes = 8;
constexpr uint32_t kBufferDescFormat = 12;
constexpr uint32_t kBufferDescMixBins = 16;

static uint32_t NewBuffer(uint32_t desc, uint32_t out) {
    std::lock_guard<std::recursive_mutex> lock(g_lock);
    Start();
    uint32_t obj = mem::AllocSystem(0x40);
    std::memset(HostPtr(obj), 0, 0x40);
    Voice& v = g_voices[obj];
    v.obj = obj;
    uint32_t flags = R32(desc + kBufferDescFlags);
    uint32_t bytes = R32(desc + kBufferDescBytes);
    ReadFormat(v, R32(desc + kBufferDescFormat));
    SetBins(v, R32(desc + kBufferDescMixBins));
    v.is3d = flags & kCaps3d;
    v.headroom = DefaultHeadroom(flags);
    if (bytes) {
        v.data = v.owned = mem::AllocContiguous(bytes, 0, 0xFFFFFFFF, 4);
        v.bytes = v.play_len = v.loop_len = bytes;
    }
    LOG_FIRST(120, "sdiag: buffer %08X tag=%04X ch=%u bits=%u rate=%u align=%u flags=%08X bytes=%u bins=%u", obj, v.tag,
              v.channels, v.bits, v.rate, v.align, flags, bytes, v.nbins);
    W32(out, obj);
    return 0;
}

HLE_STDCALL(DirectSoundCreateBuffer, 2) { return NewBuffer(a[0], a[1]); }
HLE_STDCALL(IDirectSound_CreateSoundBuffer, 4) { return NewBuffer(a[1], a[2]); }

#define BUFFER(name, nargs)                                 \
    static uint32_t buf_##name(Voice& v, const Args& a);    \
    HLE_STDCALL(IDirectSoundBuffer_##name, nargs) {         \
        std::lock_guard<std::recursive_mutex> lock(g_lock); \
        Voice* v = Find(a[0]);                              \
        return v ? buf_##name(*v, a) : kErrorFail;          \
    }                                                       \
    static uint32_t buf_##name([[maybe_unused]] Voice& v, [[maybe_unused]] const Args& a)

constexpr uint32_t kInvalidCall = 0x88780032u;  // DSERR_INVALIDCALL

BUFFER(Release, 1) {
    if (--v.refs > 0) return uint32_t(v.refs);
    uint32_t obj = v.obj;
    if (v.owned) mem::FreeContiguous(v.owned);
    g_voices.erase(obj);
    mem::FreeSystem(obj);
    return 0;
}
BUFFER(Unlock, 5) { return 0; }
BUFFER(SetBufferData, 3) {
    if (a[1] == v.data && a[2] == v.bytes) return 0;
    if (v.owned != a[1]) {
        if (v.owned) mem::FreeContiguous(v.owned);
        v.owned = 0;
    }
    v.data = a[1];
    if (!a[1] && a[2]) v.data = v.owned = mem::AllocContiguous(a[2], 0, 0xFFFFFFFF, 4);  // a size with no data
    v.bytes = v.play_len = v.loop_len = a[2];
    v.play_start = v.loop_start = 0;
    v.pos = 0;
    v.block = -1;
    return 0;
}
BUFFER(Play, 4) {
    LOG_FIRST(120, "sdiag: bufPlay %08X tag=%04X ch=%u rate=%u freq=%u bytes=%u flags=%u", v.obj, v.tag, v.channels, v.rate,
              v.freq, v.bytes, a[3]);
    v.playing = true;
    v.looping = a[3] & 1;
    if (a[3] & 2) v.pos = 0;
    v.sync = a[3] & 4;
    return 0;
}
BUFFER(Stop, 1) {
    v.playing = false;
    return 0;
}
// (this, DSBPAUSE_*): 0 resumes, 1 pauses, 2 resumes with the voice held until IDirectSound_SynchPlayback.
// The streams' Pause does the same; the mixer skips a paused voice.
BUFFER(Pause, 2) {
    v.paused = a[1] == 1;
    v.sync = a[1] == 2;
    return 0;
}
// (this, time low, time high, flags): stops now, the release envelope is not reproduced
BUFFER(StopEx, 4) {
    v.playing = false;
    return 0;
}
// (this, start, length) in bytes, 0 length plays to the end. The loop becomes the whole region.
BUFFER(SetPlayRegion, 3) {
    if (a[1] > v.bytes || a[2] > v.bytes - a[1]) return kInvalidCall;
    v.play_start = a[1];
    v.play_len = v.loop_len = a[2] ? a[2] : v.bytes - a[1];
    v.loop_start = 0;
    v.pos = 0;
    v.block = -1;
    return 0;
}
BUFFER(SetLoopRegion, 3) {
    if (a[1] > v.play_len || a[2] > v.play_len - a[1]) return kInvalidCall;
    v.loop_start = a[1];
    v.loop_len = a[2] ? a[2] : v.play_len - a[1];
    return 0;
}
BUFFER(GetStatus, 2) {
    constexpr uint32_t kStatusPlaying = 1;  // DSBSTATUS_PLAYING
    constexpr uint32_t kStatusLooping = 4;  // DSBSTATUS_LOOPING
    uint32_t status = 0;
    if (v.playing) status |= kStatusPlaying;
    if (v.playing && v.looping) status |= kStatusLooping;
    W32(a[1], status);
    return 0;
}
BUFFER(GetCurrentPosition, 3) {
    uint32_t p = BytesOf(v, uint32_t(v.pos));
    if (a[1]) W32(a[1], p);
    if (a[2]) W32(a[2], v.play_len ? (p + BytesOf(v, 64 * 4)) % v.play_len : 0);
    return 0;
}
BUFFER(SetCurrentPosition, 2) {
    v.pos = FramesOf(v, a[1]);
    return 0;
}
BUFFER(Lock, 8) {
    uint32_t off = a[1];
    uint32_t n = a[2];
    if (a[7] & 1) n = v.bytes;  // DSBLOCK_ENTIREBUFFER
    if (!v.bytes) return kErrorFail;
    off %= v.bytes;
    uint32_t first = std::min(n, v.bytes - off);
    W32(a[3], v.data + off);
    W32(a[4], first);
    if (a[5]) W32(a[5], n > first ? v.data : 0);
    if (a[6]) W32(a[6], n > first ? n - first : 0);
    return 0;
}
BUFFER(SetVolume, 2) {
    v.volume = Db(int32_t(a[1]));
    return 0;
}
BUFFER(SetFrequency, 2) {
    LOG_FIRST(120, "sdiag: bufSetFrequency %08X rate=%u -> %u", v.obj, v.rate, a[1]);
    v.freq = a[1];
    return 0;
}
BUFFER(SetFilter, 2) { return 0; }
BUFFER(SetHeadroom, 2) {
    v.headroom = Db(-int32_t(a[1]));
    return 0;
}
BUFFER(SetMixBinVolumes, 2) {
    SetBinVolumes(v, a[1]);
    return 0;
}
BUFFER(SetMixBins, 2) {
    SetBins(v, a[1]);
    return 0;
}
BUFFER(SetMaxDistance, 3) {
    v.max_dist = F32(a[1]);
    return 0;
}
BUFFER(SetMinDistance, 3) {
    v.min_dist = F32(a[1]);
    return 0;
}
BUFFER(SetPosition, 5) {
    for (int i = 0; i < 3; i++) v.p[i] = F32(a[1 + i]);
    return 0;
}
BUFFER(SetVelocity, 5) { return 0; }
BUFFER(SetI3DL2Source, 3) { return 0; }
// DS3DBUFFER
constexpr uint32_t k3dBufferPosition = 4;
constexpr uint32_t k3dBufferMinDistance = 0x34;
constexpr uint32_t k3dBufferMaxDistance = 0x38;
constexpr uint32_t k3dBufferMode = 0x3C;

// (this, DS3DBUFFER, apply)
BUFFER(SetAllParameters, 3) {
    for (int i = 0; i < 3; i++) v.p[i] = F32(R32(a[1] + k3dBufferPosition + i * 4));
    v.min_dist = F32(R32(a[1] + k3dBufferMinDistance));
    v.max_dist = F32(R32(a[1] + k3dBufferMaxDistance));
    v.mode = R32(a[1] + k3dBufferMode);
    return 0;
}
// (this, points, count, apply)
BUFFER(SetRolloffCurve, 4) {
    v.curve.clear();
    for (uint32_t i = 0; a[1] && i < std::min<uint32_t>(a[2], 64); i++) v.curve.push_back(F32(R32(a[1] + i * 4)));
    return 0;
}
BUFFER(SetOutputBuffer, 2) { return 0; }  // the voice keeps its own mix bins
BUFFER(SetFormat, 2) {
    ReadFormat(v, a[1]);
    v.freq = 0;
    v.block = -1;
    return 0;
}

// IDirectSoundBuffer::SetPitch del juego (0x003E4A2A, lo llama hooks.cpp). La voz de hardware corre a 48 kHz y el tono va en
// 1/4096 de octava respecto a esos 48 kHz: frecuencia = 48000 * 2^(pitch / 4096). Comprobado con el log: -4597 es
// exactamente 22050 Hz (4096 * log2(22050 / 48000)). Es la misma conversion que hace SetFrequency, que aqui guarda Hz.
void CodSetPitch(uint32_t obj, int32_t pitch) {
    std::lock_guard<std::recursive_mutex> lock(g_lock);
    Voice* v = Find(obj);
    if (!v) return;
    double hz = 48000.0 * std::exp2(double(pitch) / 4096.0);
    if (hz < 100.0) hz = 100.0;
    if (hz > 200000.0) hz = 200000.0;
    v->freq = uint32_t(hz + 0.5);
}
