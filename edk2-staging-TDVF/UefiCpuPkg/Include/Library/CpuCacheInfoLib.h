/** @file
  Header file for CPU Cache info Library.

  Copyright (c) 2020, Intel Corporation. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent

**/

#ifndef _CPU_CACHE_INFO_LIB_H_
#define _CPU_CACHE_INFO_LIB_H_

typedef struct {
  //
  // Package number.
  //
  UINT32        Package;
  //
  // Core type of logical processor.
  // Value = CPUID.1Ah:EAX[31:24]
  //
  UINT8         CoreType;
  //
  // Level of the cache that this package's this type of logical processor corresponds to.
  // Value = CPUID.04h:EAX[07:05]
  //
  UINT8         CacheLevel : 3;
  //
  // Type of the cache that this package's this type of logical processor corresponds to.
  // Value = CPUID.04h:EAX[04:00]
  //
  UINT8         CacheType : 5;
  //
  // Ways of associativity.
  // Value = CPUID.04h:EBX[31:22]
  //
  UINT16        CacheWays;
  //
  // Size of single cache that this package's this type of logical processor corresponds to.
  // Value = (CPUID.04h:EBX[31:22] + 1) * (CPUID.04h:EBX[21:12] + 1) *
  //         (CPUID.04h:EBX[11:00] + 1) * (CPUID.04h:ECX[31:00] + 1)
  //
  UINT32        CacheSizeinKB;
  //
  // Number of the cache that this package's this type of logical processor corresponds to.
  // Have subtracted the number of caches that are shared.
  //
  UINT16        CacheCount;
} CPU_CACHE_INFO;

/**
  Get CpuCacheInfo data array.

  @param[in, out] CpuCacheInfo        Pointer to the CpuCacheInfo array.
  @param[in, out] CpuCacheInfoCount   As input, point to the length of response CpuCacheInfo array.
                                      As output, point to the actual length of response CpuCacheInfo array.

  @retval         EFI_SUCCESS             Function completed successfully.
  @retval         EFI_INVALID_PARAMETER   CpuCacheInfoCount is NULL.
  @retval         EFI_INVALID_PARAMETER   CpuCacheInfo is NULL while CpuCacheInfoCount contains the value
                                          greater than zero.
  @retval         EFI_UNSUPPORTED         Processor does not support CPUID_CACHE_PARAMS Leaf.
  @retval         EFI_NOT_FOUND           EDKII_PEI_MP_SERVICES2_PPI or EFI_MP_SERVICES_PROTOCOL interface
                                          is not found.
  @retval         EFI_OUT_OF_RESOURCES    Required resources could not be allocated.
  @retval         EFI_BUFFER_TOO_SMALL    CpuCacheInfoCount is too small to hold the response CpuCacheInfo
                                          array. CpuCacheInfoCount has been updated with the length needed
                                          to complete the request.
**/
EFI_STATUS
EFIAPI
GetCpuCacheInfo (
  IN OUT CPU_CACHE_INFO     *CpuCacheInfo,
  IN OUT UINTN              *CpuCacheInfoCount
  );

#endif
