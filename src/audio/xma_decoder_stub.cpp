// Stub XmaDecoder + XmaContext implementation when ffmpeg is not available.
// Provides no-op implementations so AudioSystem compiles without libavcodec.

#include <rex/audio/xma/decoder.h>
#include <rex/audio/xma/context.h>
#include <rex/runtime/processor.h>
#include <rex/logging.h>

namespace rex::audio {

// XmaContext stubs
XmaContext::XmaContext() = default;
XmaContext::~XmaContext() = default;

// XmaDecoder stubs
XmaDecoder::XmaDecoder(runtime::Processor* processor)
    : memory_(processor->memory()),
      processor_(processor),
      context_bitmap_(kContextCount) {}

XmaDecoder::~XmaDecoder() = default;

X_STATUS XmaDecoder::Setup(kernel::KernelState* kernel_state) {
  REXLOG_INFO("XmaDecoder: stub (no ffmpeg) - XMA audio decoding disabled");
  return X_STATUS_SUCCESS;
}

void XmaDecoder::Shutdown() {}

uint32_t XmaDecoder::AllocateContext() { return 0; }
void XmaDecoder::ReleaseContext(uint32_t guest_ptr) {}
bool XmaDecoder::BlockOnContext(uint32_t guest_ptr, bool poll) { return true; }

uint32_t XmaDecoder::ReadRegister(uint32_t addr) { return 0; }
void XmaDecoder::WriteRegister(uint32_t addr, uint32_t value) {}

void XmaDecoder::Pause() { paused_ = true; }
void XmaDecoder::Resume() { paused_ = false; }

int XmaDecoder::GetContextId(uint32_t guest_ptr) { return -1; }
void XmaDecoder::WorkerThreadMain() {}

}  // namespace rex::audio
