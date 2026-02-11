/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 *
 * @modified    Tom Clay, 2026 - Adapted for ReXGlue runtime
 */

#include <rex/memory/utils.h>

#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <cstddef>
#include <cstdio>
#include <functional>
#include <string>

#if defined(__APPLE__)
#include <mach/mach.h>
#include <mach/mach_vm.h>
#endif

#include <rex/math.h>
#include <rex/platform.h>
#include <rex/string.h>

#if REX_PLATFORM_ANDROID
#include <dlfcn.h>
#include <linux/ashmem.h>
#include <string.h>
#include <sys/ioctl.h>

#include "xenia/base/main_android.h"
#endif

namespace rex {
namespace memory {

#if REX_PLATFORM_ANDROID
// May be null if no dynamically loaded functions are required.
static void* libandroid_;
// API 26+.
static int (*android_ASharedMemory_create_)(const char* name, size_t size);

void AndroidInitialize() {
  if (rex::GetAndroidApiLevel() >= 26) {
    libandroid_ = dlopen("libandroid.so", RTLD_NOW);
    assert_not_null(libandroid_);
    if (libandroid_) {
      android_ASharedMemory_create_ =
          reinterpret_cast<decltype(android_ASharedMemory_create_)>(
              dlsym(libandroid_, "ASharedMemory_create"));
      assert_not_null(android_ASharedMemory_create_);
    }
  }
}

void AndroidShutdown() {
  android_ASharedMemory_create_ = nullptr;
  if (libandroid_) {
    dlclose(libandroid_);
    libandroid_ = nullptr;
  }
}
#endif

size_t page_size() { return getpagesize(); }
size_t allocation_granularity() { return page_size(); }

uint32_t ToPosixProtectFlags(PageAccess access) {
  switch (access) {
    case PageAccess::kNoAccess:
      return PROT_NONE;
    case PageAccess::kReadOnly:
      return PROT_READ;
    case PageAccess::kReadWrite:
      return PROT_READ | PROT_WRITE;
    case PageAccess::kExecuteReadOnly:
      return PROT_READ | PROT_EXEC;
    case PageAccess::kExecuteReadWrite:
      return PROT_READ | PROT_WRITE | PROT_EXEC;
    default:
      assert_unhandled_case(access);
      return PROT_NONE;
  }
}

bool IsWritableExecutableMemorySupported() { return true; }

void* AllocFixed(void* base_address, size_t length,
                 AllocationType allocation_type, PageAccess access) {
  // The guest memory is backed by a shared file mapping established during
  // Memory::Initialize via MapFileView. The file-backed MAP_SHARED mapping
  // enables physical/virtual aliasing (multiple guest virtual addresses
  // mapping to the same physical backing).
  //
  // On Windows, VirtualAlloc(MEM_COMMIT) commits pages within an already-
  // reserved region. On POSIX, the equivalent is mprotect to change page
  // protection on the already-mapped file-backed pages.
  //
  // IMPORTANT: Do NOT use mmap(MAP_FIXED | MAP_ANONYMOUS) here — that would
  // replace the file-backed shared mapping with anonymous private memory,
  // breaking the physical/virtual memory aliasing.
  uint32_t prot = ToPosixProtectFlags(access);

  switch (allocation_type) {
    case AllocationType::kReserve:
      // Reserve is a no-op on POSIX — address space is already mapped
      // via the file-backed view. Just ensure pages are inaccessible.
      // Must align to system page boundaries (16KB on ARM64 macOS).
      {
        const size_t sys_page = page_size();
        uintptr_t start = reinterpret_cast<uintptr_t>(base_address);
        uintptr_t aligned_start = start & ~(sys_page - 1);
        uintptr_t aligned_end = (start + length + sys_page - 1) & ~(sys_page - 1);
        size_t aligned_length = aligned_end - aligned_start;
        if (!aligned_length) return base_address;
        if (mprotect(reinterpret_cast<void*>(aligned_start), aligned_length,
                     PROT_NONE) != 0) {
          return nullptr;
        }
      }
      return base_address;

    case AllocationType::kCommit:
    case AllocationType::kReserveCommit:
      // Commit: make pages accessible with the requested protection.
      // Must align to system page boundaries (16KB on ARM64 macOS).
      {
        const size_t sys_page = page_size();
        uintptr_t start = reinterpret_cast<uintptr_t>(base_address);
        uintptr_t aligned_start = start & ~(sys_page - 1);
        uintptr_t aligned_end = (start + length + sys_page - 1) & ~(sys_page - 1);
        size_t aligned_length = aligned_end - aligned_start;
        if (!aligned_length) return base_address;
        if (mprotect(reinterpret_cast<void*>(aligned_start), aligned_length,
                     prot) != 0) {
          return nullptr;
        }
      }
      return base_address;

    default:
      return nullptr;
  }
}

bool DeallocFixed(void* base_address, size_t length,
                  DeallocationType deallocation_type) {
  switch (deallocation_type) {
    case DeallocationType::kRelease:
    case DeallocationType::kDecommit: {
      // Do NOT munmap — that would destroy the file-backed shared mapping.
      // Instead, mprotect to PROT_NONE to make pages inaccessible (decommit).
      // Must align to system page boundaries (16KB on ARM64 macOS).
      const size_t sys_page = page_size();
      uintptr_t start = reinterpret_cast<uintptr_t>(base_address);
      uintptr_t aligned_start = start & ~(sys_page - 1);
      uintptr_t aligned_end = (start + length + sys_page - 1) & ~(sys_page - 1);
      size_t aligned_length = aligned_end - aligned_start;
      if (!aligned_length) return true;
      return mprotect(reinterpret_cast<void*>(aligned_start), aligned_length,
                      PROT_NONE) == 0;
    }
    default:
      return false;
  }
}

bool Protect(void* base_address, size_t length, PageAccess access,
             PageAccess* out_old_access) {
  // Linux does not have a syscall to query memory permissions.
  assert_null(out_old_access);

  uint32_t prot = ToPosixProtectFlags(access);
  return mprotect(base_address, length, prot) == 0;
}

bool QueryProtect(void* base_address, size_t& length, PageAccess& access_out) {
#if defined(__APPLE__)
  mach_vm_address_t address = reinterpret_cast<mach_vm_address_t>(base_address);
  mach_vm_size_t region_size = 0;
  vm_region_basic_info_data_64_t info;
  mach_msg_type_number_t info_count = VM_REGION_BASIC_INFO_COUNT_64;
  mach_port_t object_name = MACH_PORT_NULL;
  kern_return_t kr = mach_vm_region(
      mach_task_self(), &address, &region_size,
      VM_REGION_BASIC_INFO_64,
      reinterpret_cast<vm_region_info_t>(&info), &info_count, &object_name);
  if (kr != KERN_SUCCESS) {
    return false;
  }
  length = static_cast<size_t>(region_size);
  if (info.protection & VM_PROT_EXECUTE) {
    access_out = (info.protection & VM_PROT_WRITE)
                     ? PageAccess::kExecuteReadWrite
                     : PageAccess::kExecuteReadOnly;
  } else if (info.protection & VM_PROT_WRITE) {
    access_out = PageAccess::kReadWrite;
  } else if (info.protection & VM_PROT_READ) {
    access_out = PageAccess::kReadOnly;
  } else {
    access_out = PageAccess::kNoAccess;
  }
  return true;
#else
  return false;
#endif
}

FileMappingHandle CreateFileMappingHandle(const std::filesystem::path& path,
                                          size_t length, PageAccess access,
                                          bool commit) {
#if REX_PLATFORM_ANDROID
  // TODO(Triang3l): Check if memfd can be used instead on API 30+.
  if (android_ASharedMemory_create_) {
    int sharedmem_fd = android_ASharedMemory_create_(path.c_str(), length);
    return sharedmem_fd >= 0 ? sharedmem_fd : kFileMappingHandleInvalid;
  }

  // Use /dev/ashmem on API versions below 26, which added ASharedMemory.
  // /dev/ashmem was disabled on API 29 for apps targeting it.
  // https://chromium.googlesource.com/chromium/src/+/master/third_party/ashmem/ashmem-dev.c
  int ashmem_fd = open("/" ASHMEM_NAME_DEF, O_RDWR);
  if (ashmem_fd < 0) {
    return kFileMappingHandleInvalid;
  }
  char ashmem_name[ASHMEM_NAME_LEN];
  strlcpy(ashmem_name, path.c_str(), rex::countof(ashmem_name));
  if (ioctl(ashmem_fd, ASHMEM_SET_NAME, ashmem_name) < 0 ||
      ioctl(ashmem_fd, ASHMEM_SET_SIZE, length) < 0) {
    close(ashmem_fd);
    return kFileMappingHandleInvalid;
  }
  return ashmem_fd;
#else
  int oflag;
  switch (access) {
    case PageAccess::kNoAccess:
      oflag = 0;
      break;
    case PageAccess::kReadOnly:
    case PageAccess::kExecuteReadOnly:
      oflag = O_RDONLY;
      break;
    case PageAccess::kReadWrite:
    case PageAccess::kExecuteReadWrite:
      oflag = O_RDWR;
      break;
    default:
      assert_always();
      return kFileMappingHandleInvalid;
  }
  oflag |= O_CREAT;
  auto full_path = "/" / path;
  std::string shm_name = full_path.string();
#if defined(__APPLE__)
  // macOS limits POSIX shared memory names to ~30 characters.
  // Hash the name if it exceeds the limit.
  constexpr size_t kMacShmNameLimit = 30;
  if (shm_name.size() > kMacShmNameLimit) {
    size_t hash = std::hash<std::string>{}(shm_name);
    char hash_name[32] = {};
    std::snprintf(hash_name, sizeof(hash_name), "/xe_%016zx", hash);
    shm_name = hash_name;
  }
#endif
  int ret = shm_open(shm_name.c_str(), oflag, 0777);
  if (ret < 0) {
    return kFileMappingHandleInvalid;
  }
#if defined(__APPLE__)
  if (ftruncate(ret, length) != 0) {
    close(ret);
    shm_unlink(shm_name.c_str());
    return kFileMappingHandleInvalid;
  }
#else
  if (ftruncate64(ret, length) != 0) {
    close(ret);
    shm_unlink(shm_name.c_str());
    return kFileMappingHandleInvalid;
  }
#endif
  return ret;
#endif
}

void CloseFileMappingHandle(FileMappingHandle handle,
                            const std::filesystem::path& path) {
  close(handle);
#if !REX_PLATFORM_ANDROID
  auto full_path = "/" / path;
  std::string shm_name = full_path.string();
#if defined(__APPLE__)
  // Must apply the same name-hashing as CreateFileMappingHandle so
  // shm_unlink removes the correct shared memory object.
  constexpr size_t kMacShmNameLimit = 30;
  if (shm_name.size() > kMacShmNameLimit) {
    size_t hash = std::hash<std::string>{}(shm_name);
    char hash_name[32] = {};
    std::snprintf(hash_name, sizeof(hash_name), "/xe_%016zx", hash);
    shm_name = hash_name;
  }
#endif
  shm_unlink(shm_name.c_str());
#endif
}

void* MapFileView(FileMappingHandle handle, void* base_address, size_t length,
                  PageAccess access, size_t file_offset) {
  uint32_t prot = ToPosixProtectFlags(access);
#if defined(__APPLE__)
  void* result = mmap(base_address, length, prot,
                      MAP_SHARED | MAP_FIXED, handle, file_offset);
#else
  void* result = mmap64(base_address, length, prot,
                        MAP_SHARED | MAP_FIXED, handle, file_offset);
#endif
  if (result == MAP_FAILED) {
    return nullptr;
  }
  return result;
}

bool UnmapFileView(FileMappingHandle handle, void* base_address,
                   size_t length) {
  return munmap(base_address, length) == 0;
}

}  // namespace memory
}  // namespace rex
