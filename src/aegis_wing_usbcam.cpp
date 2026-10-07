// Compatibility exports for the Xbox Live Vision API imported by Aegis Wing.
// ReXGlue 0.9 contains these handlers in its source tree, but the corresponding
// translation unit is not present in the packaged Windows runtime library.

#include <rex/hook.h>
#include <rex/system/xtypes.h>

namespace {

uint32_t XUsbcamCreateCompat(uint32_t /*buffer*/,
                             uint32_t /*buffer_size*/,
                             mapped_void /*unknown*/) {
  // Match Xenia/ReXGlue's behavior. Initialization expects this allocation
  // step to succeed even when no camera is attached.
  return 0;
}

uint32_t XUsbcamGetStateCompat() {
  // Zero reports that no Xbox Live Vision camera is connected.
  return 0;
}

}  // namespace

REX_EXPORT(__imp__XUsbcamCreate, XUsbcamCreateCompat)
REX_EXPORT(__imp__XUsbcamGetState, XUsbcamGetStateCompat)

REX_EXPORT_STUB(__imp__XUsbcamDestroy)
REX_EXPORT_STUB(__imp__XUsbcamSetConfig)
REX_EXPORT_STUB(__imp__XUsbcamSetView)
REX_EXPORT_STUB(__imp__XUsbcamSetCaptureMode)
REX_EXPORT_STUB(__imp__XUsbcamReadFrame)
