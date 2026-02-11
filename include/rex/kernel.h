/**
 * @file        kernel.h
 * @brief       Umbrella header for Xbox kernel types and objects
 *
 * @copyright   Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *              All rights reserved.
 *
 * @license     BSD 3-Clause License
 *              See LICENSE file in the project root for full license text.
 */

#pragma once

// Core kernel types (X_STATUS, X_RESULT, X_HRESULT, X_HANDLE)
#include <rex/kernel/xtypes.h>

// I/O types (file attributes, overlapped)
#include <rex/kernel/xio.h>

// Kernel objects
#include <rex/kernel/xobject.h>
#include <rex/kernel/xthread.h>
#include <rex/kernel/xevent.h>
#include <rex/kernel/xmutant.h>
#include <rex/kernel/xsemaphore.h>
#include <rex/kernel/xtimer.h>
#include <rex/kernel/xfile.h>

// Memory (Xbox API wrappers - core Memory class is in rex/memory.h)
#include <rex/kernel/xmemory.h>

// Kernel state
#include <rex/kernel/kernel_state.h>
