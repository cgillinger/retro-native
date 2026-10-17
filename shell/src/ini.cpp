// Minimal INI store: [sections] with key=value lines. The file is kept as raw lines, so
// comments, ordering and unknown keys survive a rewrite; every set() writes atomically.
#include <cctype>

#include "internal.h"

namespace retro_shell {
namespace detail {
namespace {

std::string trim(const std::string &s) {
  size_t a = 0, b = s.size();
  while (a < b && isspace(static_cast<unsigned char>(s[a]))) ++a;
  while (b > a && isspace(static_cast<unsigned char>(s[b - 1]))) --b;
  return s.substr(a, b - a);
}

}  // namespace

// Section name of a "[name]" line, or false.
static bool section_of(const std::string &line, std::string &name) {
  const std::string t = trim(line);
  if (t.size() < 2 || t.front() != '[' || t.back() != ']') return false;
  name = trim(t.substr(1, t.size() - 2));
  return true;
}

// Key of a "key=value" line, or false (comments and blanks are not entries).
static bool entry_of(const std::string &line, std::string &key, std::string &value) {
  const std::string t = trim(line);
  if (t.empty() || t[0] == ';' || t[0] == '#' || t[0] == '[') return false;
  const size_t eq = t.find('=');
  if (eq == std::string::npos) return false;
  key = trim(t.substr(0, eq));
  value = trim(t.substr(eq + 1));
  return !key.empty();
}

void Ini::open(const std::string &path) {
  path_ = path;
  lines_.clear();
  std::vector<uint8_t> buf;
  if (path.empty() || !read_file(path, buf)) return;
  std::string cur;
  size_t start = 0;
  if (buf.size() >= 3 && buf[0] == 0xEF && buf[1] == 0xBB && buf[2] == 0xBF) start = 3;  // UTF-8 BOM (Notepad)
  for (size_t i = start; i < buf.size(); ++i) {
    const uint8_t c = buf[i];
    if (c == '\n') { lines_.push_back(cur); cur.clear(); }
    else if (c != '\r') cur.push_back(static_cast<char>(c));
  }
  if (!cur.empty()) lines_.push_back(cur);
}

// Index of the entry's line, or npos. `section_end` receives the index at which a new key of
// that section would be inserted (npos when the section does not exist).
size_t Ini::find(const std::string &section, const std::string &key, size_t &section_end) const {
  std::string sec, k, v;
  bool in = false;
  section_end = std::string::npos;
  size_t found = std::string::npos;
  for (size_t i = 0; i < lines_.size(); ++i) {
    if (section_of(lines_[i], sec)) {
      in = sec == section;
      if (in) section_end = i + 1;
      continue;
    }
    if (!in) continue;
    if (entry_of(lines_[i], k, v)) {
      section_end = i + 1;
      if (k == key) found = i;
    }
  }
  return found;
}

bool Ini::get(const std::string &section, const std::string &key, std::string &out) const {
  size_t end;
  const size_t i = find(section, key, end);
  if (i == std::string::npos) return false;
  std::string k;
  return entry_of(lines_[i], k, out);
}

bool Ini::write() const {
  if (path_.empty()) return true;  // in-memory only (test runs)
  std::string text;
  for (const auto &l : lines_) text += l + "\n";
  if (write_file_atomic(path_, text.data(), text.size())) return true;
  static bool warned = false;  // once per run: the settings still apply, they just will not persist
  if (!warned) {
    warned = true;
    fprintf(stderr, "settings could not be written to %s\n", path_.c_str());
    toast("SETTINGS NOT SAVED");
  }
  return false;
}

bool Ini::set(const std::string &section, const std::string &key, const std::string &value) {
  size_t end;
  const size_t i = find(section, key, end);
  const std::string line = key + "=" + value;
  if (i != std::string::npos) {
    if (lines_[i] == line) return true;
    lines_[i] = line;
  } else if (end != std::string::npos) {
    lines_.insert(lines_.begin() + static_cast<long>(end), line);
  } else {
    if (!lines_.empty() && !trim(lines_.back()).empty()) lines_.push_back("");
    lines_.push_back("[" + section + "]");
    lines_.push_back(line);
  }
  return write();
}

bool Ini::erase(const std::string &section, const std::string &key) {
  size_t end;
  const size_t i = find(section, key, end);
  if (i == std::string::npos) return true;
  lines_.erase(lines_.begin() + static_cast<long>(i));
  return write();
}

}  // namespace detail
}  // namespace retro_shell
