// Audio output: lock-free ring buffer fed through a dynamic-rate-control resampler.
#include <algorithm>
#include <atomic>

#include "internal.h"

namespace retro_shell {
namespace detail {
namespace {

constexpr uint32_t kRingFrames = 16384;  // power of two, stereo frames
int16_t g_ring[kRingFrames * 2];
std::atomic<uint32_t> g_ring_w{0}, g_ring_r{0};
SDL_AudioDeviceID g_audio_dev = 0;
int g_dev_rate = 48000;
int g_dev_buffer = 1024;
double g_res_pos = 0.0;  // fractional input position
int16_t g_prev_l = 0, g_prev_r = 0;
double g_in_rate = 32040.0;
int g_gain = 256;  // 0..256 fixed point
// Buffering: the target fill of the ring (in stereo frames) is what the rate control aims at.
// Auto starts at three device buffers and grows after underruns (STANDARD section 10), up to
// kMaxTarget; Small / Medium / Large are fixed.
constexpr int kMaxTarget = 8192;
int g_mode = ABUF_AUTO;
int g_extra = 0;                       // frames added by the adaptive part
std::atomic<uint32_t> g_underruns{0};  // counted by the callback while accounting is on
std::atomic<bool> g_counting{false};
Uint32 g_last_raise = 0;

uint32_t ring_fill() { return g_ring_w.load(std::memory_order_acquire) - g_ring_r.load(std::memory_order_acquire); }

void audio_callback(void *, Uint8 *stream, int len) {
  // Never blocks: copies what is available, pads with silence.
  int16_t *out = reinterpret_cast<int16_t *>(stream);
  uint32_t need = static_cast<uint32_t>(len) / 4;
  uint32_t r = g_ring_r.load(std::memory_order_relaxed);
  uint32_t avail = g_ring_w.load(std::memory_order_acquire) - r;
  uint32_t n = std::min(need, avail);
  for (uint32_t i = 0; i < n; ++i) {
    uint32_t idx = (r + i) & (kRingFrames - 1);
    out[i * 2] = g_ring[idx * 2];
    out[i * 2 + 1] = g_ring[idx * 2 + 1];
  }
  for (uint32_t i = n; i < need; ++i) out[i * 2] = out[i * 2 + 1] = 0;
  if (n < need && g_counting.load(std::memory_order_relaxed)) g_underruns.fetch_add(1, std::memory_order_relaxed);
  g_ring_r.store(r + n, std::memory_order_release);
}

void ring_push(int16_t l, int16_t r) {
  uint32_t w = g_ring_w.load(std::memory_order_relaxed);
  if (w - g_ring_r.load(std::memory_order_acquire) >= kRingFrames) return;  // overflow: drop
  uint32_t idx = w & (kRingFrames - 1);
  g_ring[idx * 2] = l;
  g_ring[idx * 2 + 1] = r;
  g_ring_w.store(w + 1, std::memory_order_release);
}

int target_frames() {
  switch (g_mode) {
    case ABUF_SMALL: return g_dev_buffer * 3 / 2;
    case ABUF_MEDIUM: return g_dev_buffer * 3;
    case ABUF_LARGE: return g_dev_buffer * 6;
    default: return std::min(kMaxTarget, g_dev_buffer * 3 + g_extra);
  }
}

}  // namespace

void audio_set_buffer_mode(int mode) {
  g_mode = std::max(0, std::min(ABUF_COUNT - 1, mode));
  if (g_mode != ABUF_AUTO) g_extra = 0;
}

void audio_hold(bool held) { if (held) g_counting = false; }

// Drops whatever is stale and pre-fills the ring with silence up to the target, so playback
// starts with a full cushion instead of an immediate underrun.
void audio_prefill() {
  if (!g_audio_dev) return;
  g_counting = false;
  SDL_LockAudioDevice(g_audio_dev);
  g_ring_r.store(g_ring_w.load(std::memory_order_acquire), std::memory_order_release);
  SDL_UnlockAudioDevice(g_audio_dev);
  g_res_pos = 0.0;
  g_prev_l = g_prev_r = 0;
  for (int i = 0; i < target_frames(); ++i) ring_push(0, 0);
  g_underruns = 0;
  g_counting = true;
}

bool audio_available() { return g_audio_dev != 0; }
void audio_set_input_rate(double hz) { g_in_rate = hz > 0 ? hz : 32040.0; }

bool audio_open() {
  SDL_AudioSpec want{}, have{};
  want.freq = 48000;
  want.format = AUDIO_S16SYS;
  want.channels = 2;
  want.samples = 1024;
  want.callback = audio_callback;
  g_audio_dev = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
  if (!g_audio_dev) {
    vlog("audio unavailable: %s\n", SDL_GetError());
    return false;
  }
  g_dev_rate = have.freq;
  g_dev_buffer = have.samples;
  SDL_PauseAudioDevice(g_audio_dev, 0);
  return true;
}

void audio_set_volume(int percent, bool mute) { g_gain = mute ? 0 : std::max(0, std::min(100, percent)) * 256 / 100; }

// Paused: the device stops (silence) and the stale ring content is dropped on resume.
void audio_set_paused(bool paused) {
  if (!g_audio_dev) return;
  if (paused) {
    g_counting = false;
    SDL_PauseAudioDevice(g_audio_dev, 1);
    return;
  }
  audio_prefill();
  SDL_PauseAudioDevice(g_audio_dev, 0);
}

void audio_close() {
  if (g_audio_dev) SDL_CloseAudioDevice(g_audio_dev);
  g_audio_dev = 0;
}

size_t audio_push(const int16_t *data, size_t frames) {
  const double base = g_in_rate / g_dev_rate;  // input frames consumed per output frame
  // Dynamic rate control keeps the buffer around its target despite the
  // core (50 Hz) and the host clocks never matching exactly.
  if (g_mode == ABUF_AUTO && g_underruns.exchange(0) > 0 && g_extra < kMaxTarget - g_dev_buffer * 3 &&
      SDL_GetTicks() - g_last_raise > 750) {
    g_last_raise = SDL_GetTicks();
    g_extra += g_dev_buffer / 2;
    vlog("audio: underrun, buffer target now %d frames\n", target_frames());
  }
  const double target = target_frames();
  double err = (static_cast<double>(ring_fill()) - target) / target;
  err = std::max(-1.0, std::min(1.0, err));
  const double step = base * (1.0 + 0.005 * err);
  for (size_t i = 0; i < frames; ++i) {
    int16_t l = static_cast<int16_t>(data[i * 2] * g_gain >> 8), r = static_cast<int16_t>(data[i * 2 + 1] * g_gain >> 8);
    // Emit output samples between previous and current input sample.
    while (g_res_pos < 1.0) {
      double t = g_res_pos;
      ring_push(static_cast<int16_t>(g_prev_l + (l - g_prev_l) * t),
                static_cast<int16_t>(g_prev_r + (r - g_prev_r) * t));
      g_res_pos += step;
    }
    g_res_pos -= 1.0;
    g_prev_l = l;
    g_prev_r = r;
  }
  return frames;
}

}  // namespace detail
}  // namespace retro_shell
