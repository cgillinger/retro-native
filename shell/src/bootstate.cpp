// Boot snapshot container: <magic> <u32 state size> <u64 FNV-1a of the state> <u16 n, core id>
// <u16 n, content id> <state>. Little endian. Written at build time, read at start.
#include <cstring>

#include "internal.h"

namespace retro_shell {
namespace detail {
namespace {

constexpr char kMagic[8] = {'R', 'S', 'B', 'O', 'O', 'T', '0', '1'};

uint64_t fnv1a(const uint8_t *p, size_t n) {
  uint64_t h = 1469598103934665603ull;
  for (size_t i = 0; i < n; ++i) h = (h ^ p[i]) * 1099511628211ull;
  return h;
}

void put(std::vector<uint8_t> &v, uint64_t x, int bytes) {
  for (int i = 0; i < bytes; ++i) v.push_back(static_cast<uint8_t>(x >> (8 * i)));
}

bool get(const std::vector<uint8_t> &v, size_t &pos, uint64_t &x, int bytes) {
  if (pos + static_cast<size_t>(bytes) > v.size()) return false;
  x = 0;
  for (int i = 0; i < bytes; ++i) x |= static_cast<uint64_t>(v[pos + i]) << (8 * i);
  pos += static_cast<size_t>(bytes);
  return true;
}

bool get_str(const std::vector<uint8_t> &v, size_t &pos, std::string &s) {
  uint64_t n;
  if (!get(v, pos, n, 2) || pos + n > v.size()) return false;
  s.assign(reinterpret_cast<const char *>(v.data()) + pos, static_cast<size_t>(n));
  pos += static_cast<size_t>(n);
  return true;
}

}  // namespace

std::vector<uint8_t> bootstate_pack(const std::string &core, const std::string &content_id, const void *state, size_t n) {
  std::vector<uint8_t> v(kMagic, kMagic + sizeof kMagic);
  put(v, n, 4);
  put(v, fnv1a(static_cast<const uint8_t *>(state), n), 8);
  put(v, core.size(), 2);
  v.insert(v.end(), core.begin(), core.end());
  put(v, content_id.size(), 2);
  v.insert(v.end(), content_id.begin(), content_id.end());
  v.insert(v.end(), static_cast<const uint8_t *>(state), static_cast<const uint8_t *>(state) + n);
  return v;
}

bool bootstate_unpack(const std::vector<uint8_t> &file, const std::string &core, const std::string &content_id,
                      size_t expected_size, const uint8_t *&state, size_t &n, std::string &why) {
  if (file.size() < sizeof kMagic || memcmp(file.data(), kMagic, sizeof kMagic) != 0) {
    why = "not a boot snapshot";
    return false;
  }
  size_t pos = sizeof kMagic;
  uint64_t size = 0, sum = 0;
  std::string file_core, file_content;
  if (!get(file, pos, size, 4) || !get(file, pos, sum, 8) || !get_str(file, pos, file_core) ||
      !get_str(file, pos, file_content)) {
    why = "truncated header";
    return false;
  }
  if (file_core != core) { why = "engine differs ('" + file_core + "' vs '" + core + "')"; return false; }
  if (file_content != content_id) { why = "game data differs (SHA-1 " + file_content + ")"; return false; }
  if (size != expected_size) { why = "state size differs"; return false; }
  if (file.size() - pos != size) { why = "truncated state"; return false; }
  if (fnv1a(file.data() + pos, static_cast<size_t>(size)) != sum) { why = "checksum mismatch"; return false; }
  state = file.data() + pos;
  n = static_cast<size_t>(size);
  return true;
}

}  // namespace detail
}  // namespace retro_shell
