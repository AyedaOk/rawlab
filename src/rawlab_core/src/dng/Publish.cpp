#include "rawlab/dng/Publish.h"
#include <cerrno>
#include <system_error>
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif defined(__linux__)
#include <fcntl.h>
#include <sys/syscall.h>
#include <unistd.h>
#elif defined(__APPLE__)
#include <stdio.h>
#endif

void rawlab::dng::publish(const std::filesystem::path &temporary,
                          const std::filesystem::path &destination)
{
#if defined(_WIN32)
  if(MoveFileExW(temporary.c_str(), destination.c_str(), 0)) return;
  const std::error_code error{static_cast<int>(GetLastError()), std::system_category()};
#elif defined(__linux__)
  if(::syscall(SYS_renameat2, AT_FDCWD, temporary.c_str(), AT_FDCWD, destination.c_str(), 1 /* RENAME_NOREPLACE */) == 0) return;
  const std::error_code error{errno, std::generic_category()};
#elif defined(__APPLE__)
  if(::renamex_np(temporary.c_str(), destination.c_str(), RENAME_EXCL) == 0) return;
  const std::error_code error{errno, std::generic_category()};
#else
  std::filesystem::create_hard_link(temporary, destination);
  std::error_code ignored;
  std::filesystem::remove(temporary, ignored);
  return;
#endif
#if defined(_WIN32) || defined(__linux__) || defined(__APPLE__)
  throw std::filesystem::filesystem_error{"Cannot publish DNG", temporary, destination, error};
#endif
}
