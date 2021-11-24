/** @file

  There are 4 defined types in TD memory.
  Unaccepted memory is a special type of private memory. The guest
  firmware must invoke TDCALL [TDG.MEM.PAGE.ACCEPT] the unaccepted
  memory before use it.

  Copyright (c) 2020 - 2021, Intel Corporation. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent

**/

#include <Library/BaseLib.h>
#include <Library/DebugLib.h>
#include <IndustryStandard/Tdx.h>
#include <Library/TdxLib.h>
#include <Library/BaseMemoryLib.h>

UINT64  mNumberOfDuplicatedAcceptedPages;

UINT64  mTdxAcceptPageLevelMap[2] = {
  SIZE_4KB,
  SIZE_2MB
};

UINTN
GetGpaPageLevel (
  UINT64 PageSize
  )
{
  UINTN Index;

  for (Index = 0; Index < sizeof (mTdxAcceptPageLevelMap) / sizeof (mTdxAcceptPageLevelMap[0]); Index++) {
    if (mTdxAcceptPageLevelMap[Index] == PageSize) {
      break;
    }
  }

  return Index;
}

/**
  This function accept a pending private page, and initialize the page to
  all-0 using the TD ephemeral private key.

  @param[in]  StartAddress      Guest physical address of the private
                                page to accept.
  @param[in]  NumberOfPages     Number of the pages to be accepted.
  @param[in]  PageSize          GPA page size. Only accept 1G/2M/4K size.

  @return EFI_SUCCESS
**/
EFI_STATUS
EFIAPI
TdAcceptPages (
  IN UINT64  StartAddress,
  IN UINT64  NumberOfPages,
  IN UINT64  PageSize
  )
{
  EFI_STATUS  Status;
  UINT64      Address;
  UINT64      TdxStatus;
  UINT64      Index;
  UINT64      GpaPageLevel;
  UINT64      PageSize2;

  Address = StartAddress;

  GpaPageLevel = (UINT64) GetGpaPageLevel (PageSize);
  if (GpaPageLevel > sizeof (mTdxAcceptPageLevelMap) / sizeof (mTdxAcceptPageLevelMap[0])) {
    DEBUG ((DEBUG_ERROR, "Accept page size must be 4K/2M. Invalid page size - 0x%llx\n", PageSize));
    return EFI_INVALID_PARAMETER;
  }

  Status = EFI_SUCCESS;
  for (Index = 0; Index < NumberOfPages; Index++) {
    TdxStatus = TdCall (TDCALL_TDACCEPTPAGE, Address | GpaPageLevel, 0, 0, 0);
    if (TdxStatus != TDX_EXIT_REASON_SUCCESS) {
        if ((TdxStatus & ~0xFFFFULL) == TDX_EXIT_REASON_PAGE_ALREADY_ACCEPTED) {
          //
          // Already accepted
          //
          mNumberOfDuplicatedAcceptedPages++;
          DEBUG ((DEBUG_VERBOSE, "Address %llx already accepted. Total number of already accepted pages %ld\n",
            Address, mNumberOfDuplicatedAcceptedPages));
        } else if ((TdxStatus & ~0xFFFFULL) == TDX_EXIT_REASON_PAGE_SIZE_MISMATCH) {
          //
          // GpaPageLevel is mismatch, fall back to a smaller GpaPageLevel if available
          //
          DEBUG ((DEBUG_VERBOSE, "Address %llx cannot be accepted in PageLevel of %d\n", Address, GpaPageLevel));

          if (GpaPageLevel == 0) {
            //
            // Cannot fall back to smaller page size
            //
            DEBUG ((DEBUG_ERROR, "AcceptPage cannot fallback from PageLevel %d\n", GpaPageLevel));
            Status = EFI_INVALID_PARAMETER;
            break;
          } else {
            //
            // Fall back to a smaller page size
            //
            PageSize2 = mTdxAcceptPageLevelMap [GpaPageLevel - 1];
            Status = TdAcceptPages(Address, 512, PageSize2);
            if (EFI_ERROR (Status)) {
              break;
            }
          }
        }else {

          //
          // Other errors
          //
          DEBUG ((DEBUG_ERROR, "Address %llx (%d) failed to be accepted. Error = 0x%llx\n",
            Address, Index, TdxStatus));
          Status = EFI_INVALID_PARAMETER;
          break;
        }
    }
    Address += PageSize;
  }
  return Status;
}
