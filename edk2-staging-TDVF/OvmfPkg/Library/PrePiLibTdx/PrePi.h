/** @file
  Library that helps implement monolithic PEI (i.e. PEI part of SEC)

  Copyright (c) 2008 - 2009, Apple Inc. All rights reserved.<BR>

  SPDX-License-Identifier: BSD-2-Clause-Patent

**/

#ifndef _PI_PEI_H_
#define _PI_PEI_H_

#include <PiPei.h>

#include <Library/BaseLib.h>
#include <Library/PrePiLibTdx.h>
#include <Library/PcdLib.h>
#include <Library/DebugLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/UefiDecompressLib.h>
#include <Library/PeCoffLib.h>
#include <Library/CacheMaintenanceLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/TimerLib.h>
#include <Library/PerformanceLib.h>
#include <Library/HobLib.h>
#include <Guid/MemoryAllocationHob.h>

#endif
