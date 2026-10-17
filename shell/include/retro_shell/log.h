#pragma once

namespace retro_shell {

// stderr output, only with --verbose.
void vlog(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
bool verbose();

}  // namespace retro_shell
