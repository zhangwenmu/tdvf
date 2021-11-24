/** @file

  Copyright (c) 2021, Intel Corporation. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent

**/

#include <Library/BaseLib.h>
#include <Library/DebugLib.h>
#include <IndustryStandard/Tdx.h>
#include <Library/TdxLib.h>
#include <Library/BaseMemoryLib.h>

UINT64  mTdSharedPageMask = 0;
UINT32  mTdMaxVCpuNum     = 0;

/**
  This function gets the Td guest shared page mask.

  The guest indicates if a page is shared using the Guest Physical Address
  (GPA) Shared (S) bit. If the GPA Width(GPAW) is 48, the S-bit is bit-47.
  If the GPAW is 52, the S-bit is bit-51.

  @return Shared page bit mask
**/
UINT64
EFIAPI
TdSharedPageMask (
  VOID
  )
{
  UINT64                      Status;
  UINT8                       Gpaw;
  TD_RETURN_DATA              TdReturnData;

  if (mTdSharedPageMask != 0) {
    return mTdSharedPageMask;
  }

  Status = TdCall (TDCALL_TDINFO, 0,0,0, &TdReturnData);
  ASSERT (Status == TDX_EXIT_REASON_SUCCESS);

  Gpaw = (UINT8)(TdReturnData.TdInfo.Gpaw & 0x3f);
  mTdSharedPageMask = 1ULL << (Gpaw - 1);
  ASSERT(Gpaw == 48 || Gpaw == 52);
  return mTdSharedPageMask;
}

/**
  This function gets the maximum number of Virtual CPUs that are usable for
  Td Guest.

  @return maximum Virtual CPUs number
**/
UINT32
EFIAPI
TdMaxVCpuNum (
  VOID
  )
{
  UINT64                      Status;
  TD_RETURN_DATA              TdReturnData;

  if (mTdMaxVCpuNum != 0) {
    return mTdMaxVCpuNum;
  }

  Status = TdCall (TDCALL_TDINFO, 0,0,0, &TdReturnData);
  ASSERT (Status == TDX_EXIT_REASON_SUCCESS);

  return TdReturnData.TdInfo.MaxVcpus;
}

/**
  This function gets the number of Virtual CPUs that are usable for Td 
  Guest.

  @return Virtual CPUs number
**/
UINT32
EFIAPI
TdVCpuNum (
  VOID
  )
{
  UINT64                      Status;
  TD_RETURN_DATA              TdReturnData;

  //
  // Due to the possible change of vcpu num in run-time, do the Td call
  // for every query.
  //
  Status = TdCall (TDCALL_TDINFO, 0,0,0, &TdReturnData);
  ASSERT (Status == TDX_EXIT_REASON_SUCCESS);

  return TdReturnData.TdInfo.NumVcpus;
}

