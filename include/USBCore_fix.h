// Workaround for TinyUSB macro conflict
// This prevents the MSC_SUBCLASS_SCSI macro from interfering with enum definitions

#ifndef USBCORE_FIX_H
#define USBCORE_FIX_H

// Save the conflicting macro
#ifdef MSC_SUBCLASS_SCSI
  #define MSC_SUBCLASS_SCSI_SAVE MSC_SUBCLASS_SCSI
  #undef MSC_SUBCLASS_SCSI
#endif

#endif // USBCORE_FIX_H
