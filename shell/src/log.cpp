#include "retro_shell/log.h"

#include <cstdarg>
#include <cstdio>

#include "internal.h"

namespace retro_shell {

bool verbose() { return detail::g_opt.verbose; }

void vlog(const char *fmt, ...) {
  if (!detail::g_opt.verbose) return;
  va_list ap;
  va_start(ap, fmt);
  vfprintf(stderr, fmt, ap);
  va_end(ap);
}

}  // namespace retro_shell
