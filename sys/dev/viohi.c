// viohi.c
//

#include "virtio.h"
#include "intr.h"
#include "heap.h"
#include "io.h"
#include "device.h"
#include "thread.h"
#include "error.h"
#include "string.h"
#include "ioimpl.h"
#include "assert.h"
#include "conf.h"
#include "console.h"

// INTERNAL CONSTANT DEFINITIONS

#ifndef VIOHI_NAME
#define VIOHI_NAME "input"
#endif

#ifndef VIOHI_IRQ_PRIO
#define VIOHI_IRQ_PRIO 1
#endif

// INTERNAL TYPE DEFINITIONS
//

struct viohi_device
{

}
