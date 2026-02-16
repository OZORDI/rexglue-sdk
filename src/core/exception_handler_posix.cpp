/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 *
 * @modified    Tom Clay, 2026 - Adapted for ReXGlue runtime
 */

#include <rex/exception_handler.h>

#if REX_PLATFORM_LINUX || REX_PLATFORM_MAC

#ifdef __APPLE__
#define _XOPEN_SOURCE
#endif
#include <signal.h>
#include <ucontext.h>
#include <cstdint>
#include <cstring>

#include <unistd.h>

#include <rex/assert.h>
#include <rex/logging.h>
#include <rex/math.h>
#include <rex/platform.h>

// Signal-safe logging helpers — fprintf is NOT async-signal-safe and will
// deadlock if another thread holds stderr's FILE* lock when the signal fires.
namespace {

void ss_write(const char* s) {
  if (s) write(STDERR_FILENO, s, strlen(s));
}

void ss_hex(uint64_t val) {
  char buf[19];  // "0x" + 16 hex digits + NUL
  buf[0] = '0'; buf[1] = 'x';
  for (int i = 17; i >= 2; --i) {
    buf[i] = "0123456789abcdef"[val & 0xf];
    val >>= 4;
  }
  buf[18] = '\0';
  write(STDERR_FILENO, buf, 18);
}

void ss_dec(uint32_t val) {
  char buf[11];
  int i = 10;
  buf[i] = '\0';
  if (val == 0) { buf[--i] = '0'; }
  else { while (val) { buf[--i] = '0' + (val % 10); val /= 10; } }
  write(STDERR_FILENO, buf + i, 10 - i);
}

}  // anonymous namespace

namespace rex::arch {

bool signal_handlers_installed_ = false;
struct sigaction original_sigill_handler_;
struct sigaction original_sigsegv_handler_;
#if REX_PLATFORM_MAC
struct sigaction original_sigbus_handler_;
#endif

// This can be as large as needed, but isn't often needed.
// As we will be sometimes firing many exceptions we want to avoid having to
// scan the table too much or invoke many custom handlers.
constexpr size_t kMaxHandlerCount = 8;

// All custom handlers, left-aligned and null terminated.
// Executed in order.
std::pair<ExceptionHandler::Handler, void*> handlers_[kMaxHandlerCount];

static void ExceptionHandlerCallback(int signal_number, siginfo_t* signal_info,
                                     void* signal_context) {
  // Diagnostic: log entry to exception handler (truly signal-safe via write())
  {
    static std::atomic<uint32_t> s_entry_count{0};
    uint32_t n = s_entry_count.fetch_add(1, std::memory_order_relaxed);
    if (n < 15 || (n < 10000 && (n & (n - 1)) == 0)) {
      ss_write("[EXC-ENTRY] sig=");
      ss_dec(static_cast<uint32_t>(signal_number));
      ss_write(" addr=");
      ss_hex(reinterpret_cast<uint64_t>(signal_info->si_addr));
      ss_write(" #");
      ss_dec(n + 1);
      ss_write("\n");
    }
  }

#if REX_PLATFORM_MAC
  // On macOS, mcontext_t is already a pointer (_STRUCT_MCONTEXT64*)
  mcontext_t mcontext =
      reinterpret_cast<ucontext_t*>(signal_context)->uc_mcontext;
#else
  mcontext_t& mcontext =
      reinterpret_cast<ucontext_t*>(signal_context)->uc_mcontext;
#endif

  HostThreadContext thread_context;

#if REX_ARCH_AMD64
#if REX_PLATFORM_MAC
  // macOS x86_64: mcontext uses _STRUCT_MCONTEXT64 with __ss (state) and __fs (float state)
  thread_context.rip = uint64_t(mcontext->__ss.__rip);
  thread_context.eflags = uint32_t(mcontext->__ss.__rflags);
  thread_context.rax = uint64_t(mcontext->__ss.__rax);
  thread_context.rcx = uint64_t(mcontext->__ss.__rcx);
  thread_context.rdx = uint64_t(mcontext->__ss.__rdx);
  thread_context.rbx = uint64_t(mcontext->__ss.__rbx);
  thread_context.rsp = uint64_t(mcontext->__ss.__rsp);
  thread_context.rbp = uint64_t(mcontext->__ss.__rbp);
  thread_context.rsi = uint64_t(mcontext->__ss.__rsi);
  thread_context.rdi = uint64_t(mcontext->__ss.__rdi);
  thread_context.r8 = uint64_t(mcontext->__ss.__r8);
  thread_context.r9 = uint64_t(mcontext->__ss.__r9);
  thread_context.r10 = uint64_t(mcontext->__ss.__r10);
  thread_context.r11 = uint64_t(mcontext->__ss.__r11);
  thread_context.r12 = uint64_t(mcontext->__ss.__r12);
  thread_context.r13 = uint64_t(mcontext->__ss.__r13);
  thread_context.r14 = uint64_t(mcontext->__ss.__r14);
  thread_context.r15 = uint64_t(mcontext->__ss.__r15);
  std::memcpy(thread_context.xmm_registers,
              &mcontext->__fs.__fpu_xmm0,
              sizeof(thread_context.xmm_registers));
#else  // Linux x86_64
  thread_context.rip = uint64_t(mcontext.gregs[REG_RIP]);
  thread_context.eflags = uint32_t(mcontext.gregs[REG_EFL]);
  // The REG_ order may be different than the register indices in the
  // instruction encoding.
  thread_context.rax = uint64_t(mcontext.gregs[REG_RAX]);
  thread_context.rcx = uint64_t(mcontext.gregs[REG_RCX]);
  thread_context.rdx = uint64_t(mcontext.gregs[REG_RDX]);
  thread_context.rbx = uint64_t(mcontext.gregs[REG_RBX]);
  thread_context.rsp = uint64_t(mcontext.gregs[REG_RSP]);
  thread_context.rbp = uint64_t(mcontext.gregs[REG_RBP]);
  thread_context.rsi = uint64_t(mcontext.gregs[REG_RSI]);
  thread_context.rdi = uint64_t(mcontext.gregs[REG_RDI]);
  thread_context.r8 = uint64_t(mcontext.gregs[REG_R8]);
  thread_context.r9 = uint64_t(mcontext.gregs[REG_R9]);
  thread_context.r10 = uint64_t(mcontext.gregs[REG_R10]);
  thread_context.r11 = uint64_t(mcontext.gregs[REG_R11]);
  thread_context.r12 = uint64_t(mcontext.gregs[REG_R12]);
  thread_context.r13 = uint64_t(mcontext.gregs[REG_R13]);
  thread_context.r14 = uint64_t(mcontext.gregs[REG_R14]);
  thread_context.r15 = uint64_t(mcontext.gregs[REG_R15]);
  std::memcpy(thread_context.xmm_registers, mcontext.fpregs->_xmm,
              sizeof(thread_context.xmm_registers));
#endif  // REX_PLATFORM_MAC
#elif REX_ARCH_ARM64
#if REX_PLATFORM_MAC
  // macOS ARM64: mcontext uses _STRUCT_MCONTEXT64 with __ss (general regs),
  // __es (exception state), __ns (NEON/FP state)
  for (int i = 0; i < 29; ++i) {
    thread_context.x[i] = mcontext->__ss.__x[i];
  }
  thread_context.x[29] = mcontext->__ss.__fp;  // Frame pointer = x29
  thread_context.x[30] = mcontext->__ss.__lr;  // Link register = x30
  thread_context.sp = mcontext->__ss.__sp;
  thread_context.pc = mcontext->__ss.__pc;
  thread_context.pstate = mcontext->__ss.__cpsr;
  thread_context.fpsr = mcontext->__ns.__fpsr;
  thread_context.fpcr = mcontext->__ns.__fpcr;
  static_assert(sizeof(thread_context.v) == sizeof(mcontext->__ns.__v),
                "NEON register sizes must match");
  std::memcpy(thread_context.v, mcontext->__ns.__v,
              sizeof(thread_context.v));
#else  // Linux ARM64
  std::memcpy(thread_context.x, mcontext.regs, sizeof(thread_context.x));
  thread_context.sp = mcontext.sp;
  thread_context.pc = mcontext.pc;
  thread_context.pstate = mcontext.pstate;
  struct fpsimd_context* mcontext_fpsimd = nullptr;
  struct esr_context* mcontext_esr = nullptr;
  for (struct _aarch64_ctx* mcontext_extension =
           reinterpret_cast<struct _aarch64_ctx*>(mcontext.__reserved);
       mcontext_extension->magic;
       mcontext_extension = reinterpret_cast<struct _aarch64_ctx*>(
           reinterpret_cast<uint8_t*>(mcontext_extension) +
           mcontext_extension->size)) {
    switch (mcontext_extension->magic) {
      case FPSIMD_MAGIC:
        mcontext_fpsimd =
            reinterpret_cast<struct fpsimd_context*>(mcontext_extension);
        break;
      case ESR_MAGIC:
        mcontext_esr =
            reinterpret_cast<struct esr_context*>(mcontext_extension);
        break;
      default:
        break;
    }
  }
  assert_not_null(mcontext_fpsimd);
  if (mcontext_fpsimd) {
    thread_context.fpsr = mcontext_fpsimd->fpsr;
    thread_context.fpcr = mcontext_fpsimd->fpcr;
    std::memcpy(thread_context.v, mcontext_fpsimd->vregs,
                sizeof(thread_context.v));
  }
#endif  // REX_PLATFORM_MAC
#endif  // REX_ARCH

  Exception ex;
  switch (signal_number) {
    case SIGILL:
      ex.InitializeIllegalInstruction(&thread_context);
      break;
    case SIGBUS:  // macOS delivers SIGBUS for some memory access violations
    case SIGSEGV: {
      Exception::AccessViolationOperation access_violation_operation;
#if REX_ARCH_AMD64
#if REX_PLATFORM_MAC
      // macOS x86_64: fault type is in __es.__err
      constexpr uint64_t kX86PageFaultErrorCodeWrite = UINT64_C(1) << 1;
      access_violation_operation =
          (uint64_t(mcontext->__es.__err) & kX86PageFaultErrorCodeWrite)
              ? Exception::AccessViolationOperation::kWrite
              : Exception::AccessViolationOperation::kRead;
#else
      // x86_pf_error_code::X86_PF_WRITE
      constexpr uint64_t kX86PageFaultErrorCodeWrite = UINT64_C(1) << 1;
      access_violation_operation =
          (uint64_t(mcontext.gregs[REG_ERR]) & kX86PageFaultErrorCodeWrite)
              ? Exception::AccessViolationOperation::kWrite
              : Exception::AccessViolationOperation::kRead;
#endif  // REX_PLATFORM_MAC
#elif REX_ARCH_ARM64
#if REX_PLATFORM_MAC
      // macOS ARM64: Use ESR from __es.__esr for data abort detection
      {
        uint64_t esr = mcontext->__es.__esr;
        if (((esr >> 26) & 0b111110) == 0b100100) {
          // Data Abort: bit 6 indicates write vs read
          access_violation_operation =
              (esr & (UINT64_C(1) << 6))
                  ? Exception::AccessViolationOperation::kWrite
                  : Exception::AccessViolationOperation::kRead;
        } else if (mcontext->__ss.__pc == 0) {
          // NULL PC — call/jump through NULL function pointer.
          // Cannot dereference PC to decode the instruction; doing so
          // would cause a recursive SIGSEGV that deadlocks the thread
          // on macOS (Mach exception inside signal handler).
          access_violation_operation =
              Exception::AccessViolationOperation::kRead;
          ss_write("[EXC-NULL-PC] Jump to NULL detected\n");
        } else {
          // Fall back to instruction decoding
          bool instruction_is_store;
          if (IsArm64LoadPrefetchStore(
                  *reinterpret_cast<const uint32_t*>(mcontext->__ss.__pc),
                  instruction_is_store)) {
            access_violation_operation =
                instruction_is_store
                    ? Exception::AccessViolationOperation::kWrite
                    : Exception::AccessViolationOperation::kRead;
          } else {
            access_violation_operation =
                Exception::AccessViolationOperation::kUnknown;
          }
        }
      }
#else  // Linux ARM64
      // For a Data Abort (EC - ESR_EL1 bits 31:26 - 0b100100 from a lower
      // Exception Level, 0b100101 without a change in the Exception Level),
      // bit 6 is 0 for reading from a memory location, 1 for writing to a
      // memory location.
      if (mcontext_esr && ((mcontext_esr->esr >> 26) & 0b111110) == 0b100100) {
        access_violation_operation =
            (mcontext_esr->esr & (UINT64_C(1) << 6))
                ? Exception::AccessViolationOperation::kWrite
                : Exception::AccessViolationOperation::kRead;
      } else {
        // Determine the memory access direction based on which instruction has
        // requested it.
        // esr_context may be unavailable on certain hosts (for instance, on
        // Android, it was added only in NDK r16 - which is the first NDK
        // version to support the Android API level 27, while NDK r15 doesn't
        // have esr_context in its API 26 sigcontext.h).
        // On AArch64 (unlike on AArch32), the program counter is the address of
        // the currently executing instruction.
        bool instruction_is_store;
        if (IsArm64LoadPrefetchStore(
                *reinterpret_cast<const uint32_t*>(mcontext.pc),
                instruction_is_store)) {
          access_violation_operation =
              instruction_is_store ? Exception::AccessViolationOperation::kWrite
                                   : Exception::AccessViolationOperation::kRead;
        } else {
          assert_always(
              "No ESR in the exception thread context, or it's not a Data "
              "Abort, and the faulting instruction is not a known load, "
              "prefetch or store instruction");
          access_violation_operation =
              Exception::AccessViolationOperation::kUnknown;
        }
      }
#endif  // REX_PLATFORM_MAC (ARM64)
#else
      access_violation_operation =
          Exception::AccessViolationOperation::kUnknown;
#endif  // REX_ARCH
      ex.InitializeAccessViolation(
          &thread_context, reinterpret_cast<uint64_t>(signal_info->si_addr),
          access_violation_operation);
    } break;
    default:
      assert_unhandled_case(signal_number);
  }

#if REX_ARCH_ARM64
  // Handle NULL PC faults (call/jump through NULL function pointer).
  // In recompiled code, this means an indirect call resolved to a function
  // that wasn't recompiled or linked. Skip it by returning to LR.
  if (signal_number == SIGSEGV && thread_context.pc == 0) {
    uint64_t lr = thread_context.x[30];
    ss_write("[EXC-NULL-CALL] Skipping NULL call, returning to LR=");
    ss_hex(lr);
    ss_write("\n");
    mcontext->__ss.__pc = lr;
    // Return 0 in x0 (common convention for "failure" return)
    mcontext->__ss.__x[0] = 0;
    return;
  }
#endif

  for (size_t i = 0; i < rex::countof(handlers_) && handlers_[i].first; ++i) {
    bool handled = handlers_[i].first(&ex, handlers_[i].second);
    // Diagnostic: log handler results (signal-safe)
    {
      static std::atomic<uint32_t> s_dispatch_count{0};
      uint32_t dn = s_dispatch_count.fetch_add(1, std::memory_order_relaxed);
      if (dn < 15 || (dn < 10000 && (dn & (dn - 1)) == 0)) {
        ss_write("[EXC-DISPATCH] handler[");
        ss_dec(static_cast<uint32_t>(i));
        ss_write("] ");
        ss_write(handled ? "TRUE" : "FALSE");
        ss_write(" pc=");
        ss_hex(thread_context.pc);
        ss_write(" #");
        ss_dec(dn + 1);
        ss_write("\n");
      }
    }
    if (handled) {
      // Exception handled.
      // NOTE: If we return from the signal handler after handling, the kernel
      // will resume execution at the (possibly modified) PC.
#if REX_ARCH_AMD64
#if REX_PLATFORM_MAC
      mcontext->__ss.__rip = thread_context.rip;
      mcontext->__ss.__rflags = thread_context.eflags;
      // Writeback modified registers
      // Using direct field access for macOS mcontext
      uint64_t* mac_int_regs[] = {
          &mcontext->__ss.__rax, &mcontext->__ss.__rcx,
          &mcontext->__ss.__rdx, &mcontext->__ss.__rbx,
          &mcontext->__ss.__rsp, &mcontext->__ss.__rbp,
          &mcontext->__ss.__rsi, &mcontext->__ss.__rdi,
          &mcontext->__ss.__r8,  &mcontext->__ss.__r9,
          &mcontext->__ss.__r10, &mcontext->__ss.__r11,
          &mcontext->__ss.__r12, &mcontext->__ss.__r13,
          &mcontext->__ss.__r14, &mcontext->__ss.__r15,
      };
      uint32_t modified_register_index;
      uint16_t modified_int_registers_remaining = ex.modified_int_registers();
      while (rex::bit_scan_forward(modified_int_registers_remaining,
                                  &modified_register_index)) {
        modified_int_registers_remaining &=
            ~(UINT16_C(1) << modified_register_index);
        *mac_int_regs[modified_register_index] =
            thread_context.int_registers[modified_register_index];
      }
      uint16_t modified_xmm_registers_remaining = ex.modified_xmm_registers();
      while (rex::bit_scan_forward(modified_xmm_registers_remaining,
                                  &modified_register_index)) {
        modified_xmm_registers_remaining &=
            ~(UINT16_C(1) << modified_register_index);
        std::memcpy(
            reinterpret_cast<uint8_t*>(&mcontext->__fs.__fpu_xmm0) +
                modified_register_index * sizeof(vec128_t),
            &thread_context.xmm_registers[modified_register_index],
            sizeof(vec128_t));
      }
#else  // Linux x86_64
      mcontext.gregs[REG_RIP] = greg_t(thread_context.rip);
      mcontext.gregs[REG_EFL] = greg_t(thread_context.eflags);
      uint32_t modified_register_index;
      // The order must match the order in X64Register.
      static const size_t kIntRegisterMap[] = {
          REG_RAX, REG_RCX, REG_RDX, REG_RBX, REG_RSP, REG_RBP,
          REG_RSI, REG_RDI, REG_R8,  REG_R9,  REG_R10, REG_R11,
          REG_R12, REG_R13, REG_R14, REG_R15,
      };
      uint16_t modified_int_registers_remaining = ex.modified_int_registers();
      while (rex::bit_scan_forward(modified_int_registers_remaining,
                                  &modified_register_index)) {
        modified_int_registers_remaining &=
            ~(UINT16_C(1) << modified_register_index);
        mcontext.gregs[kIntRegisterMap[modified_register_index]] =
            thread_context.int_registers[modified_register_index];
      }
      uint16_t modified_xmm_registers_remaining = ex.modified_xmm_registers();
      while (rex::bit_scan_forward(modified_xmm_registers_remaining,
                                  &modified_register_index)) {
        modified_xmm_registers_remaining &=
            ~(UINT16_C(1) << modified_register_index);
        std::memcpy(&mcontext.fpregs->_xmm[modified_register_index],
                    &thread_context.xmm_registers[modified_register_index],
                    sizeof(vec128_t));
      }
#endif  // REX_PLATFORM_MAC
#elif REX_ARCH_ARM64
#if REX_PLATFORM_MAC
      {
        uint32_t modified_register_index;
        uint32_t modified_x_registers_remaining = ex.modified_x_registers();
        while (rex::bit_scan_forward(modified_x_registers_remaining,
                                    &modified_register_index)) {
          modified_x_registers_remaining &=
              ~(UINT32_C(1) << modified_register_index);
          if (modified_register_index < 29) {
            mcontext->__ss.__x[modified_register_index] =
                thread_context.x[modified_register_index];
          } else if (modified_register_index == 29) {
            mcontext->__ss.__fp = thread_context.x[29];
          } else if (modified_register_index == 30) {
            mcontext->__ss.__lr = thread_context.x[30];
          }
        }
        mcontext->__ss.__sp = thread_context.sp;
        mcontext->__ss.__pc = thread_context.pc;
        mcontext->__ss.__cpsr = uint32_t(thread_context.pstate);
        mcontext->__ns.__fpsr = thread_context.fpsr;
        mcontext->__ns.__fpcr = thread_context.fpcr;
        uint32_t modified_v_registers_remaining = ex.modified_v_registers();
        while (rex::bit_scan_forward(modified_v_registers_remaining,
                                    &modified_register_index)) {
          modified_v_registers_remaining &=
              ~(UINT32_C(1) << modified_register_index);
          std::memcpy(&mcontext->__ns.__v[modified_register_index],
                      &thread_context.v[modified_register_index],
                      sizeof(vec128_t));
        }
      }
#else  // Linux ARM64
      uint32_t modified_register_index;
      uint32_t modified_x_registers_remaining = ex.modified_x_registers();
      while (rex::bit_scan_forward(modified_x_registers_remaining,
                                  &modified_register_index)) {
        modified_x_registers_remaining &=
            ~(UINT32_C(1) << modified_register_index);
        mcontext.regs[modified_register_index] =
            thread_context.x[modified_register_index];
      }
      mcontext.sp = thread_context.sp;
      mcontext.pc = thread_context.pc;
      mcontext.pstate = thread_context.pstate;
      if (mcontext_fpsimd) {
        mcontext_fpsimd->fpsr = thread_context.fpsr;
        mcontext_fpsimd->fpcr = thread_context.fpcr;
        uint32_t modified_v_registers_remaining = ex.modified_v_registers();
        while (rex::bit_scan_forward(modified_v_registers_remaining,
                                    &modified_register_index)) {
          modified_v_registers_remaining &=
              ~(UINT32_C(1) << modified_register_index);
          std::memcpy(&mcontext_fpsimd->vregs[modified_register_index],
                      &thread_context.v[modified_register_index],
                      sizeof(vec128_t));
          mcontext.regs[modified_register_index] =
              thread_context.x[modified_register_index];
        }
      }
#endif  // REX_PLATFORM_MAC
#endif  // REX_ARCH
      return;
    }
  }

  // No handler handled the exception.
  // On macOS, some system library faults (CoreText, etc.) appear as SIGSEGV
  // but are resolved by the Mach VM on the next attempt. We return with our
  // handler still installed — the kernel re-executes the instruction, the VM
  // maps the page, and execution continues. A per-address retry counter
  // prevents infinite loops for truly fatal faults.
#if REX_PLATFORM_MAC
  {
    static thread_local uint64_t last_fault_addr = 0;
    static thread_local int fault_retry_count = 0;
    uint64_t addr = reinterpret_cast<uint64_t>(signal_info->si_addr);
    if (addr == last_fault_addr) {
      ++fault_retry_count;
    } else {
      last_fault_addr = addr;
      fault_retry_count = 1;
    }
    if (fault_retry_count <= 4) {
      // Let the Mach VM resolve this fault — just return with our handler
      // still installed. The kernel will re-execute the faulting instruction.
      return;
    }
    // Exceeded retry limit — this is a truly fatal fault.
    last_fault_addr = 0;
    fault_retry_count = 0;
  }
#endif
  // Fatal: restore the original handler and re-raise.
  switch (signal_number) {
    case SIGILL:
      sigaction(SIGILL, &original_sigill_handler_, nullptr);
      break;
    case SIGSEGV:
      sigaction(SIGSEGV, &original_sigsegv_handler_, nullptr);
      break;
#if REX_PLATFORM_MAC
    case SIGBUS:
      sigaction(SIGBUS, &original_sigbus_handler_, nullptr);
      break;
#endif
  }
  ss_write("[FATAL] Unhandled ");
  ss_write(signal_number == SIGSEGV ? "SIGSEGV" :
           signal_number == SIGBUS  ? "SIGBUS"  :
           signal_number == SIGILL  ? "SIGILL"  : "signal");
  ss_write(" at addr=");
  ss_hex(reinterpret_cast<uint64_t>(signal_info->si_addr));
  ss_write("\n");
  raise(signal_number);
}

void ExceptionHandler::ReinstallSignalHandlers() {
  // Force re-registration of ExceptionHandlerCallback as the active POSIX
  // signal handler.  Call this after another subsystem (e.g. SEH/guest)
  // has installed its own handler on top.  The previous (SEH) handler is
  // saved in original_sig*_handler_ so ExceptionHandlerCallback can fall
  // back to it for faults it doesn't handle.
  struct sigaction sa;
  std::memset(&sa, 0, sizeof(sa));
  sa.sa_sigaction = ExceptionHandlerCallback;
  sa.sa_flags = SA_SIGINFO;

  sigaction(SIGILL,  &sa, &original_sigill_handler_);
  sigaction(SIGSEGV, &sa, &original_sigsegv_handler_);
#if REX_PLATFORM_MAC
  sigaction(SIGBUS,  &sa, &original_sigbus_handler_);
#endif
  signal_handlers_installed_ = true;
}

void ExceptionHandler::Install(Handler fn, void* data) {
  if (!signal_handlers_installed_) {
    struct sigaction signal_handler;

    std::memset(&signal_handler, 0, sizeof(signal_handler));
    signal_handler.sa_sigaction = ExceptionHandlerCallback;
    signal_handler.sa_flags = SA_SIGINFO;

    if (sigaction(SIGILL, &signal_handler, &original_sigill_handler_) != 0) {
      assert_always("Failed to install new SIGILL handler");
    }
    if (sigaction(SIGSEGV, &signal_handler, &original_sigsegv_handler_) != 0) {
      assert_always("Failed to install new SIGSEGV handler");
    }
#if REX_PLATFORM_MAC
    // macOS delivers SIGBUS for certain memory access violations
    // (e.g., unaligned access, accessing unmapped memory via mach_vm)
    if (sigaction(SIGBUS, &signal_handler, &original_sigbus_handler_) != 0) {
      assert_always("Failed to install new SIGBUS handler");
    }
#endif
    signal_handlers_installed_ = true;
  }

  for (size_t i = 0; i < rex::countof(handlers_); ++i) {
    if (!handlers_[i].first) {
      handlers_[i].first = fn;
      handlers_[i].second = data;
      return;
    }
  }
  assert_always("Too many exception handlers installed");
}

void ExceptionHandler::Uninstall(Handler fn, void* data) {
  for (size_t i = 0; i < rex::countof(handlers_); ++i) {
    if (handlers_[i].first == fn && handlers_[i].second == data) {
      for (; i < rex::countof(handlers_) - 1; ++i) {
        handlers_[i] = handlers_[i + 1];
      }
      handlers_[i].first = nullptr;
      handlers_[i].second = nullptr;
      break;
    }
  }

  bool has_any = false;
  for (size_t i = 0; i < rex::countof(handlers_); ++i) {
    if (handlers_[i].first) {
      has_any = true;
      break;
    }
  }
  if (!has_any) {
    if (signal_handlers_installed_) {
      if (sigaction(SIGILL, &original_sigill_handler_, NULL) != 0) {
        assert_always("Failed to restore original SIGILL handler");
      }
      if (sigaction(SIGSEGV, &original_sigsegv_handler_, NULL) != 0) {
        assert_always("Failed to restore original SIGSEGV handler");
      }
#if REX_PLATFORM_MAC
      if (sigaction(SIGBUS, &original_sigbus_handler_, NULL) != 0) {
        assert_always("Failed to restore original SIGBUS handler");
      }
#endif
      signal_handlers_installed_ = false;
    }
  }
}

}  // namespace rex::arch

#endif  // REX_PLATFORM_LINUX || REX_PLATFORM_MAC
