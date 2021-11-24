/** @file

Copyright (c) 2018 - 2019, Intel Corporation. All rights reserved.<BR>
This program and the accompanying materials 
are licensed and made available under the terms and conditions of the BSD License 
which accompanies this distribution.  The full text of the license may be found at 
http://opensource.org/licenses/bsd-license.php

THE PROGRAM IS DISTRIBUTED UNDER THE BSD LICENSE ON AN "AS IS" BASIS, 
WITHOUT WARRANTIES OR REPRESENTATIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED.

**/

#include <PiDxe.h>

#include <Protocol/Tcg2Protocol.h>
#include <Protocol/Tdx.h>

#include <Library/DebugLib.h>
#include <Library/DebugPrintErrorLevelLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/BaseLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/PrintLib.h>
#include <Library/Tpm2CommandLib.h>
#include <Library/ShellLib.h>
#include <Library/UefiLib.h>
#include <Library/BaseCryptLib.h>
#include <Library/DevicePathLib.h>
#include <IndustryStandard/Acpi.h>
#include <IndustryStandard/Tpm2Acpi.h>
#include <IndustryStandard/Spdm.h>
#include <IndustryStandard/Tdx.h>
#include <Library/TdxLib.h>

#define EV_NO_ACTION                ((TCG_EVENTTYPE) 0x00000003)
#define MAX_TDX_REG_INDEX   4
#define TD_REPORT_DATA_SIZE     64

typedef struct {
  EFI_TD_EVENT_LOG_FORMAT  LogFormat;
} EFI_TD_EVENT_INFO_STRUCT;


typedef
UINTN
(EFIAPI *EFI_HASH_GET_CONTEXT_SIZE) (
  VOID
  );

typedef
BOOLEAN
(EFIAPI *EFI_HASH_INIT) (
  OUT  VOID  *HashContext
  );

typedef
BOOLEAN
(EFIAPI *EFI_HASH_UPDATE) (
  IN OUT  VOID        *HashContext,
  IN      CONST VOID  *Data,
  IN      UINTN       DataSize
  );

typedef
BOOLEAN
(EFIAPI *EFI_HASH_FINAL) (
  IN OUT  VOID   *HashContext,
  OUT     UINT8  *HashValue
  );

typedef struct {
  TPM_ALG_ID                 HashAlg;
  EFI_HASH_GET_CONTEXT_SIZE  GetContextSize;
  EFI_HASH_INIT              Init;
  EFI_HASH_UPDATE            Update;
  EFI_HASH_FINAL             Final;
} EFI_HASH_INFO;



typedef struct{
  TPMI_ALG_HASH   HashAlgo;
  UINT16          HashSize;
  UINT32          HashMask;
}TDX_HASH_INFO;

TDX_HASH_INFO mTdxHashInfo[] = {
  {TPM_ALG_SHA384, SHA384_DIGEST_SIZE, HASH_ALG_SHA384}
};

EFI_HASH_INFO  mHashInfo[] = {
  {TPM_ALG_SHA1,   Sha1GetContextSize,   Sha1Init,   Sha1Update,   Sha1Final,   },
  {TPM_ALG_SHA256, Sha256GetContextSize, Sha256Init, Sha256Update, Sha256Final, },
  {TPM_ALG_SHA384, Sha384GetContextSize, Sha384Init, Sha384Update, Sha384Final,}
};
#define INDEX_ALL 0xFFFFFFFF

SHELL_PARAM_ITEM mParamList[] = {
  {L"-I",   TypeValue},
  {L"-E",   TypeFlag},
  {L"-A",   TypeFlag},
  {L"-R",   TypeFlag},
  {L"-?",   TypeFlag},
  {L"-h",   TypeFlag},
  {NULL,    TypeMax},
  };

/**

  This function dump raw data.

  @param  Data  raw data
  @param  Size  raw data size

**/
VOID
InternalDumpData (
  IN UINT8  *Data,
  IN UINTN  Size
  )
{
  UINTN  Index;
  for (Index = 0; Index < Size; Index++) {
    Print (L"%02x", (UINTN)Data[Index]);
  }
}


/**

  This function dump raw data with colume format.

  @param  Data  raw data
  @param  Size  raw data size

**/
VOID
InternalDumpHex (
  IN UINT8  *Data,
  IN UINTN  Size
  )
{
  UINTN   Index;
  UINTN   Count;
  UINTN   Left;

#define COLUME_SIZE  (16 * 2)

  Count = Size / COLUME_SIZE;
  Left  = Size % COLUME_SIZE;
  for (Index = 0; Index < Count; Index++) {
    Print (L"%04x: ", Index * COLUME_SIZE);
    InternalDumpData (Data + Index * COLUME_SIZE, COLUME_SIZE);
    Print (L"\n");
  }

  if (Left != 0) {
    Print (L"%04x: ", Index * COLUME_SIZE);
    InternalDumpData (Data + Index * COLUME_SIZE, Left);
    Print (L"\n");
  }
}



/**
  Dump RTMR data.
  

**/

VOID 
DumpRtmr(
  IN UINT8   *ReportBuffer,
  IN UINT32 ReportSize,
  IN UINT8  *AdditionalData,
  IN UINT32  DataSize
)
{
  EFI_STATUS    Status;
  UINT8 *mReportBuffer;
  UINT32 mReportSize;
  UINT8 *mAdditionalData;
  UINT32 mDataSize;
  mReportBuffer =ReportBuffer;
  mReportSize = ReportSize;
  mAdditionalData = AdditionalData;
  mDataSize = DataSize;
  Status = DoTdReport(mReportBuffer, mReportSize, mAdditionalData, mDataSize);
  if (EFI_ERROR (Status)){
    Print (L"ReadTdReport - %r\n", Status);
  return;
}
}


EFI_HASH_INFO *
GetHashInfo (
  IN     TPM_ALG_ID  HashAlg
  )
{
  UINTN      Index;

  for (Index = 0; Index < sizeof(mHashInfo)/sizeof(mHashInfo[0]); Index++) {
    if (HashAlg == mHashInfo[Index].HashAlg) {
      return &mHashInfo[Index];
    }
  }
  return NULL;
}

/**
  Get hash size based on Algo

  @param[in]     HashAlgo           Hash Algorithm Id.

  @return Size of the hash.
**/
UINT16
GetHashSizeByAlgo(
  IN TPMI_ALG_HASH HashAlgo
  )
{
  UINTN Index;

  for(Index = 0; Index < sizeof(mTdxHashInfo)/sizeof(mTdxHashInfo[0]); Index++){
    if(mTdxHashInfo[Index].HashAlgo == HashAlgo){
      return mTdxHashInfo[Index].HashSize;
    }
  }

  return 0;
}
VOID
ExtendEvent (
  IN     TPM_ALG_ID  HashAlg,
  IN OUT VOID        *TcgDigest,
  IN     VOID        *NewDigest
  )
{
  VOID                     *HashCtx;
  UINTN                    CtxSize;
  UINT16                   DigestSize;
  EFI_HASH_INFO            *HashInfo;

  DigestSize = GetHashSizeByAlgo (HashAlg);

  HashInfo = GetHashInfo (HashAlg);
  if (HashInfo == NULL) {
    SetMem (TcgDigest, DigestSize, 0xFF);
    return ;
  }

  CtxSize = HashInfo->GetContextSize ();
  HashCtx = AllocatePool (CtxSize);
  if (HashCtx == NULL) {
    SetMem (TcgDigest, DigestSize, 0xFF);
    return ;
  }
  HashInfo->Init (HashCtx);
  HashInfo->Update (HashCtx, TcgDigest, DigestSize);
  HashInfo->Update (HashCtx, NewDigest, DigestSize);
  HashInfo->Final (HashCtx, (UINT8 *)TcgDigest);
  FreePool (HashCtx);
}

VOID
DumpTcgSp800155PlatformIdEvent2Struct (
  IN TCG_Sp800_155_PlatformId_Event2   *TcgSp800155PlatformIdEvent2Struct
  )
{
  UINTN                            Index;
  UINT8                            *StrSize;
  UINT8                            *StrBuffer;
  UINT32                           *Id;

  Print (L"  TcgSp800155PlatformIdEvent2Struct:\n");
  Print (L"    signature                   - '");
  for (Index = 0; Index < sizeof(TcgSp800155PlatformIdEvent2Struct->Signature); Index++) {
    Print (L"%c", TcgSp800155PlatformIdEvent2Struct->Signature[Index]);
  }
  Print (L"'\n");
  Print (L"    VendorId                    - 0x%08x\n", TcgSp800155PlatformIdEvent2Struct->VendorId);
  Print (L"    ReferenceManifestGuid       - %g\n", &TcgSp800155PlatformIdEvent2Struct->ReferenceManifestGuid);

  StrSize = (UINT8 *)(TcgSp800155PlatformIdEvent2Struct + 1);
  StrBuffer = StrSize + 1;
  Print (L"    PlatformManufacturerStrSize - 0x%02x\n", *StrSize);
  Print (L"    PlatformManufacturerStr     - %a\n", StrBuffer);

  StrSize = (UINT8 *)(StrBuffer + *StrSize);
  StrBuffer = StrSize + 1;
  Print (L"    PlatformModelSize           - 0x%02x\n", *StrSize);
  Print (L"    PlatformModel               - %a\n", StrBuffer);

  StrSize = (UINT8 *)(StrBuffer + *StrSize);
  StrBuffer = StrSize + 1;
  Print (L"    PlatformVersionSize         - 0x%02x\n", *StrSize);
  Print (L"    PlatformVersion             - %a\n", StrBuffer);

  StrSize = (UINT8 *)(StrBuffer + *StrSize);
  StrBuffer = StrSize + 1;
  Print (L"    FirmwareManufacturerStrSize - 0x%02x\n", *StrSize);
  Print (L"    FirmwareManufacturerStr     - %a\n", StrBuffer);

  Id = (UINT32 *)(StrBuffer + *StrSize);
  Print (L"    FirmwareManufacturerId      - 0x%08x\n", *Id);

  StrSize = (UINT8 *)(Id + 1);
  StrBuffer = StrSize + 1;
  Print (L"    FirmwareVersionSize         - 0x%02x\n", *StrSize);
  Print (L"    FirmwareVersion             - %a\n", StrBuffer);
}

VOID
DumpTcgStartupLocalityEventStruct (
  IN TCG_EfiStartupLocalityEvent   *TcgStartupLocalityEventStruct
  )
{
  UINTN  Index;

  Print (L"  TcgStartupLocalityEventStruct:\n");
  Print (L"    signature       - '");
  for (Index = 0; Index < sizeof(TcgStartupLocalityEventStruct->Signature); Index++) {
    Print (L"%c", TcgStartupLocalityEventStruct->Signature[Index]);
  }
  Print (L"'\n");
  Print (L"    StartupLocality - 0x%02x\n", TcgStartupLocalityEventStruct->StartupLocality);
}

VOID
ParseEventData (
  IN TCG_EVENTTYPE         EventType,
  IN UINT8                 *EventBuffer,
  IN UINTN                 EventSize
  )
{
  UINTN                                         Index;

  UEFI_VARIABLE_DATA                            *UefiVariableData;
  UINT8                                         *VariableData;
  
  EFI_IMAGE_LOAD_EVENT                          *EfiImageLoadEvent;

  EFI_PLATFORM_FIRMWARE_BLOB                    *EfiPlatformFirmwareBlob;
  UEFI_PLATFORM_FIRMWARE_BLOB                   *UefiPlatformFirmwareBlob;
  UEFI_PLATFORM_FIRMWARE_BLOB2                  *UefiPlatformFirmwareBlob2;
  EFI_HANDOFF_TABLE_POINTERS                    *EfiHandoffTablePointers;
  UEFI_HANDOFF_TABLE_POINTERS                   *UefiHandoffTablePointers;
  UEFI_HANDOFF_TABLE_POINTERS2                  *UefiHandoffTablePointers2;

  TCG_DEVICE_SECURITY_EVENT_DATA_HEADER         *EventDataHeader;
  SPDM_MEASUREMENT_BLOCK_COMMON_HEADER          *CommonHeader;
  SPDM_MEASUREMENT_BLOCK_DMTF_HEADER            *DmtfHeader;
  UINT8                                         *MeasurementBuffer;
  TCG_DEVICE_SECURITY_EVENT_DATA_PCI_CONTEXT    *PciContext;

  InternalDumpHex (EventBuffer, EventSize);

  switch (EventType) {
  case EV_POST_CODE:
    Print(L"    EventData - Type: EV_POST_CODE\n");
    Print(L"      POST CODE - \"");

    for (Index = 0; Index < EventSize; Index++) {
      Print(L"%c", EventBuffer[Index]);
    }
    Print(L"\"\n");

    break;

  case EV_NO_ACTION:
    Print(L"    EventData - Type: EV_NO_ACTION\n");

    if ((EventSize >= sizeof(TCG_Sp800_155_PlatformId_Event2)) &&
        (CompareMem (EventBuffer, TCG_Sp800_155_PlatformId_Event2_SIGNATURE, sizeof(TCG_Sp800_155_PlatformId_Event2_SIGNATURE) - 1) == 0)) {
      DumpTcgSp800155PlatformIdEvent2Struct ((TCG_Sp800_155_PlatformId_Event2 *)EventBuffer);

      break;
    }

    if ((EventSize >= sizeof(TCG_EfiStartupLocalityEvent)) &&
        (CompareMem (EventBuffer, TCG_EfiStartupLocalityEvent_SIGNATURE, sizeof(TCG_EfiStartupLocalityEvent_SIGNATURE)) == 0)) {
      DumpTcgStartupLocalityEventStruct ((TCG_EfiStartupLocalityEvent *)EventBuffer);

      break;
    }
    
    Print(L"  Unknown EV_NO_ACTION\n");

    break;

  case EV_SEPARATOR:
    Print(L"    EventData - Type: EV_SEPARATOR\n");
    Print(L"      SEPARATOR - 0x%08x\n", *(UINT32*)EventBuffer);

    break;

  case EV_S_CRTM_VERSION:
    Print(L"    EventData - Type: EV_S_CRTM_VERSION\n");
    Print(L"      CRTM VERSION - L\"");

    for (Index = 0; Index < EventSize; Index+=2) {
      Print(L"%c", EventBuffer[Index]);
    }
    Print(L"\"\n");

    break;

  case EV_EFI_VARIABLE_DRIVER_CONFIG:
  case EV_EFI_VARIABLE_BOOT:
    if (EventType == EV_EFI_VARIABLE_DRIVER_CONFIG) {
      Print(L"    EventData - Type: EV_EFI_VARIABLE_DRIVER_CONFIG\n");
    } else if (EventType == EV_EFI_VARIABLE_BOOT) {
      Print(L"    EventData - Type: EV_EFI_VARIABLE_BOOT\n");
    }

    UefiVariableData = (UEFI_VARIABLE_DATA*)EventBuffer;
    Print(L"      VariableName       - %g\n", UefiVariableData->VariableName);
    Print(L"      UnicodeNameLength  - 0x%016x\n", UefiVariableData->UnicodeNameLength);
    Print(L"      VariableDataLength - 0x%016x\n", UefiVariableData->VariableDataLength);

    Print(L"      UnicodeName        - ");
    for (Index = 0; Index < UefiVariableData->UnicodeNameLength; Index++) {
      Print(L"%c", UefiVariableData->UnicodeName[Index]);
    }
    Print(L"\n");

    VariableData = (UINT8*)&UefiVariableData->UnicodeName[Index];
    Print(L"      VariableData       - ");

    for (Index = 0; Index < UefiVariableData->VariableDataLength; Index++) {
      Print(L"%02x ", VariableData[Index]);

      if ((Index + 1) % 0x10 == 0) {
        Print(L"\n");
        if (Index + 1 < UefiVariableData->VariableDataLength) {
          Print(L"                           ");
        }
      }
    }

    if (UefiVariableData->VariableDataLength == 0 || UefiVariableData->VariableDataLength % 0x10 != 0) {
      Print(L"\n");
    }

    break;

  case EV_EFI_BOOT_SERVICES_APPLICATION:
  case EV_EFI_BOOT_SERVICES_DRIVER:
  case EV_EFI_RUNTIME_SERVICES_DRIVER:
    if (EventType == EV_EFI_BOOT_SERVICES_APPLICATION) {
      Print(L"    EventData - Type: EV_EFI_BOOT_SERVICES_APPLICATION\n");
    } else if (EventType == EV_EFI_BOOT_SERVICES_DRIVER) {
      Print(L"    EventData - Type: EV_EFI_BOOT_SERVICES_DRIVER\n");
    } else if (EventType == EV_EFI_RUNTIME_SERVICES_DRIVER) {
      Print(L"    EventData - Type: EV_EFI_RUNTIME_SERVICES_DRIVER\n");
    }

    EfiImageLoadEvent = (EFI_IMAGE_LOAD_EVENT*)EventBuffer;
    Print(L"      ImageLocationInMemory - 0x%016x\n", EfiImageLoadEvent->ImageLocationInMemory);
    Print(L"      ImageLengthInMemory   - 0x%016x\n", EfiImageLoadEvent->ImageLengthInMemory);
    Print(L"      ImageLinkTimeAddress  - 0x%016x\n", EfiImageLoadEvent->ImageLinkTimeAddress);
    Print(L"      LengthOfDevicePath    - 0x%016x\n", EfiImageLoadEvent->LengthOfDevicePath);
    Print(L"      DevicePath:\n");
    Print(L"        %s\n", ConvertDevicePathToText(EfiImageLoadEvent->DevicePath, FALSE, FALSE));

    break;

  case EV_EFI_ACTION:
    Print(L"    EventData - Type: EV_EFI_ACTION\n");
    Print(L"      Action String - \"");

    for (Index = 0; Index < EventSize; Index++) {
      Print(L"%c", EventBuffer[Index]);
    }
    Print(L"\"\n");

    break;

  case EV_PLATFORM_CONFIG_FLAGS:
    Print(L"    EventData - Type: EV_PLATFORM_CONFIG_FLAGS\n");
    Print(L"      Platform Config - \"");

    for (Index = 0; Index < EventSize; Index++) {
      Print(L"%c", EventBuffer[Index]);
    }
    Print(L"\"\n");
    break;

  case EV_EFI_PLATFORM_FIRMWARE_BLOB:
    EfiPlatformFirmwareBlob = (EFI_PLATFORM_FIRMWARE_BLOB*)EventBuffer;
    Print(L"    EventData - Type: EV_EFI_PLATFORM_FIRMWARE_BLOB\n");
    Print(L"      BlobBase   - 0x%016x\n", EfiPlatformFirmwareBlob->BlobBase);
    Print(L"      BlobLength - 0x%016x\n", EfiPlatformFirmwareBlob->BlobLength);

    break;

  case EV_EFI_PLATFORM_FIRMWARE_BLOB2:
    UefiPlatformFirmwareBlob2 = (UEFI_PLATFORM_FIRMWARE_BLOB2*)EventBuffer;
    UefiPlatformFirmwareBlob = (UEFI_PLATFORM_FIRMWARE_BLOB*)(EventBuffer +
                                 sizeof(UefiPlatformFirmwareBlob2->BlobDescriptionSize) +
                                 UefiPlatformFirmwareBlob2->BlobDescriptionSize);
    Print(L"    EventData - Type: EV_EFI_PLATFORM_FIRMWARE_BLOB2\n");
    Print(L"      BlobDescriptionSize - 0x%02x\n", UefiPlatformFirmwareBlob2->BlobDescriptionSize);
    Print(L"      BlobDescription     - \"");
    for (Index = 0; Index < UefiPlatformFirmwareBlob2->BlobDescriptionSize; Index++) {
      Print(L"%c", *(EventBuffer + sizeof(UefiPlatformFirmwareBlob2->BlobDescriptionSize) + Index));
    }
    Print(L"\"\n");
    Print(L"      BlobBase   - 0x%016x\n", UefiPlatformFirmwareBlob->BlobBase);
    Print(L"      BlobLength - 0x%016x\n", UefiPlatformFirmwareBlob->BlobLength);

    break;

  case EV_EFI_HANDOFF_TABLES:
    EfiHandoffTablePointers = (EFI_HANDOFF_TABLE_POINTERS*)EventBuffer;
    Print(L"    EventData - Type: EV_EFI_HANDOFF_TABLES\n");
    Print(L"      NumberOfTables - 0x%016x\n", EfiHandoffTablePointers->NumberOfTables);
    for (Index = 0; Index < EfiHandoffTablePointers->NumberOfTables; Index++) {
      Print(L"      TableEntry (%d):\n", Index);
      Print(L"        VendorGuid  - %g\n", EfiHandoffTablePointers->TableEntry[Index].VendorGuid);
      Print(L"        VendorTable - 0x%016x\n", EfiHandoffTablePointers->TableEntry[Index].VendorTable);
    }
    break;

  case EV_EFI_HANDOFF_TABLES2:
    UefiHandoffTablePointers2 = (UEFI_HANDOFF_TABLE_POINTERS2*)EventBuffer;
    UefiHandoffTablePointers = (UEFI_HANDOFF_TABLE_POINTERS*)(EventBuffer +
                                 sizeof(UefiHandoffTablePointers2->TableDescriptionSize) +
                                 UefiHandoffTablePointers2->TableDescriptionSize);
    Print(L"    EventData - Type: EV_EFI_HANDOFF_TABLES2\n");
    Print(L"      TableDescriptionSize - 0x%02x\n", UefiHandoffTablePointers2->TableDescriptionSize);
    Print(L"      TableDescription     - \"");
    for (Index = 0; Index < UefiHandoffTablePointers2->TableDescriptionSize; Index++) {
      Print(L"%c", *(EventBuffer + sizeof(UefiHandoffTablePointers2->TableDescriptionSize) + Index));
    }
    Print(L"\"\n");

    Print(L"      NumberOfTables - 0x%016x\n", UefiHandoffTablePointers->NumberOfTables);
    for (Index = 0; Index < UefiHandoffTablePointers->NumberOfTables; Index++) {
      Print(L"      TableEntry (%d):\n", Index);
      Print(L"        VendorGuid  - %g\n", UefiHandoffTablePointers->TableEntry[Index].VendorGuid);
      Print(L"        VendorTable - 0x%016x\n", UefiHandoffTablePointers->TableEntry[Index].VendorTable);
    }

    break;

  case EV_EFI_SPDM_FIRMWARE_BLOB:
  case EV_EFI_SPDM_FIRMWARE_CONFIG:
    if (EventType == EV_EFI_SPDM_FIRMWARE_BLOB) {
      Print(L"    EventData - Type: EV_EFI_SPDM_FIRMWARE_BLOB\n");
    } else if (EventType == EV_EFI_SPDM_FIRMWARE_CONFIG) {
      Print(L"    EventData - Type: EV_EFI_SPDM_FIRMWARE_CONFIG\n");
    }
    EventDataHeader = (TCG_DEVICE_SECURITY_EVENT_DATA_HEADER*)EventBuffer;
    Print(L"      Signature         - '");
    for (Index = 0; Index < sizeof(EventDataHeader->Signature); Index++) {
        Print(L"%c", EventDataHeader->Signature[Index]);
    }
    Print(L"'\n");
    Print(L"      Version           - 0x%04x\n", EventDataHeader->Version);
    Print(L"      Length            - 0x%04x\n", EventDataHeader->Length);
    Print(L"      SpdmHashAlgo      - 0x%08x\n", EventDataHeader->SpdmHashAlgo);
    Print(L"      DeviceType        - 0x%08x\n", EventDataHeader->DeviceType);

    Print(L"      SpdmMeasurementBlock:\n");
    CommonHeader = (SPDM_MEASUREMENT_BLOCK_COMMON_HEADER*)((UINT8*)EventDataHeader + sizeof(TCG_DEVICE_SECURITY_EVENT_DATA_HEADER));
    Print(L"        Index             - 0x%02x\n", CommonHeader->Index);
    Print(L"        MeasurementSpec   - 0x%02x\n", CommonHeader->MeasurementSpecification);
    Print(L"        MeasurementSize   - 0x%04x\n", CommonHeader->MeasurementSize);

    Print(L"        Measurement:\n");
    DmtfHeader = (SPDM_MEASUREMENT_BLOCK_DMTF_HEADER*)((UINT8*)CommonHeader + sizeof(SPDM_MEASUREMENT_BLOCK_COMMON_HEADER));
    Print(L"          DMTFSpecMeasurementValueType - 0x%02x\n", DmtfHeader->DMTFSpecMeasurementValueType);
    Print(L"          DMTFSpecMeasurementValueSize - 0x%04x\n", DmtfHeader->DMTFSpecMeasurementValueSize);
    Print(L"          DMTFSpecMeasurementValue     - ");
    MeasurementBuffer = (UINT8*)((UINT8*)DmtfHeader + sizeof(SPDM_MEASUREMENT_BLOCK_DMTF_HEADER));
    for (Index = 0; Index < DmtfHeader->DMTFSpecMeasurementValueSize; Index++) {
        Print(L"%02x", MeasurementBuffer[Index]);
    }
    Print(L"\n");

    switch (EventDataHeader->DeviceType) {
    case TCG_DEVICE_SECURITY_EVENT_DATA_DEVICE_TYPE_NULL:
      Print(L"      DeviceSecurityEventData - No Context\n");
      break;
    case TCG_DEVICE_SECURITY_EVENT_DATA_DEVICE_TYPE_PCI:
      Print(L"      DeviceSecurityEventData - PCI Context\n");
      PciContext = (TCG_DEVICE_SECURITY_EVENT_DATA_PCI_CONTEXT*)(MeasurementBuffer + DmtfHeader->DMTFSpecMeasurementValueSize);
      Print(L"        Version           - 0x%04x\n", PciContext->Version);
      Print(L"        Length            - 0x%04x\n", PciContext->Length);
      Print(L"        VendorId          - 0x%04x\n", PciContext->VendorId);
      Print(L"        DeviceId          - 0x%04x\n", PciContext->DeviceId);
      Print(L"        RevisionID        - 0x%02x\n", PciContext->RevisionID);
      Print(L"        ClassCode         - 0x%06x\n", PciContext->ClassCode[2] << 16 | PciContext->ClassCode[1] << 8| PciContext->ClassCode[0]);
      Print(L"        SubsystemVendorID - 0x%04x\n", PciContext->SubsystemVendorID);
      Print(L"        SubsystemID       - 0x%04x\n", PciContext->SubsystemID);
      break;
    case TCG_DEVICE_SECURITY_EVENT_DATA_DEVICE_TYPE_USB:
      Print(L"      DeviceSecurityEventData - USB Context\n");
      break;
    default:
      Print(L"      DeviceSecurityEventData - Reserved\n");
    }
    
    break;

  default:
    Print(L"Unknown Event Type\n");
    break;
  }
}

/**
  This function dump TCG_EfiSpecIDEventStruct.

  @param[in]  TcgEfiSpecIdEventStruct     A pointer to TCG_EfiSpecIDEventStruct.
**/
VOID
DumpTcgEfiSpecIdEventStruct (
  IN TCG_EfiSpecIDEventStruct   *TcgEfiSpecIdEventStruct
  )
{
  TCG_EfiSpecIdEventAlgorithmSize  *digestSize;
  UINTN                            Index;
  UINT8                            *vendorInfoSize;
  UINT8                            *vendorInfo;
  UINT32                           numberOfAlgorithms;

  Print (L"  TCG_EfiSpecIDEventStruct:\n");
  Print (L"    signature          - '");
  for (Index = 0; Index < sizeof(TcgEfiSpecIdEventStruct->signature); Index++) {
    Print (L"%c", TcgEfiSpecIdEventStruct->signature[Index]);
  }
  Print (L"'\n");
  Print (L"    platformClass      - 0x%08x\n", TcgEfiSpecIdEventStruct->platformClass);
  Print (L"    specVersion        - %d.%d.%d\n", TcgEfiSpecIdEventStruct->specVersionMajor, TcgEfiSpecIdEventStruct->specVersionMinor, TcgEfiSpecIdEventStruct->specErrata);
  Print (L"    uintnSize          - 0x%02x\n", TcgEfiSpecIdEventStruct->uintnSize);

  CopyMem (&numberOfAlgorithms, TcgEfiSpecIdEventStruct + 1, sizeof(numberOfAlgorithms));
  Print (L"    numberOfAlgorithms - 0x%08x\n", numberOfAlgorithms);

  digestSize = (TCG_EfiSpecIdEventAlgorithmSize *)((UINT8 *)TcgEfiSpecIdEventStruct + sizeof(*TcgEfiSpecIdEventStruct) + sizeof(numberOfAlgorithms));
  for (Index = 0; Index < numberOfAlgorithms; Index++) {
    Print (L"    digest(%d)\n", Index);
    Print (L"      algorithmId      - 0x%04x\n", digestSize[Index].algorithmId);
    Print (L"      digestSize       - 0x%04x\n", digestSize[Index].digestSize);
  }
  vendorInfoSize = (UINT8 *)&digestSize[numberOfAlgorithms];
  Print (L"    vendorInfoSize     - 0x%02x\n", *vendorInfoSize);
  vendorInfo = vendorInfoSize + 1;
  Print (L"    vendorInfo         - ");
  for (Index = 0; Index < *vendorInfoSize; Index++) {
    Print (L"%02x", vendorInfo[Index]);
  }
  Print (L"\n");
}

/**
  This function get size of TCG_EfiSpecIDEventStruct.

  @param[in]  TcgEfiSpecIdEventStruct     A pointer to TCG_EfiSpecIDEventStruct.
**/
UINTN
GetTcgEfiSpecIdEventStructSize (
  IN TCG_EfiSpecIDEventStruct   *TcgEfiSpecIdEventStruct
  )
{
  TCG_EfiSpecIdEventAlgorithmSize  *digestSize;
  UINT8                            *vendorInfoSize;
  UINT32                           numberOfAlgorithms;

  CopyMem (&numberOfAlgorithms, TcgEfiSpecIdEventStruct + 1, sizeof(numberOfAlgorithms));

  digestSize = (TCG_EfiSpecIdEventAlgorithmSize *)((UINT8 *)TcgEfiSpecIdEventStruct + sizeof(*TcgEfiSpecIdEventStruct) + sizeof(numberOfAlgorithms));
  vendorInfoSize = (UINT8 *)&digestSize[numberOfAlgorithms];
  return sizeof(TCG_EfiSpecIDEventStruct) + sizeof(UINT32) + (numberOfAlgorithms * sizeof(TCG_EfiSpecIdEventAlgorithmSize)) + sizeof(UINT8) + (*vendorInfoSize);
}

VOID
DumpTdvfEvent (
  IN TCG_PCR_EVENT_HDR         *EventHdr
  )
{
  UINTN                     Index;

  Print (L"  Event:\n");
  Print (L"   Mr Index  - %d\n", EventHdr->PCRIndex);
  Print (L"    EventType - 0x%08x\n", EventHdr->EventType);
  Print (L"    Digest    - ");
  for (Index = 0; Index < sizeof(TCG_DIGEST); Index++) {
    Print (L"%02x", EventHdr->Digest.digest[Index]);
  }
  Print (L"\n");
  Print (L"    EventSize - 0x%08x\n", EventHdr->EventSize);
  ParseEventData (EventHdr->EventType, (UINT8 *)(EventHdr + 1), EventHdr->EventSize);
}

VOID
DumpTdvfEvent2 (
  IN TD_EVENT        *TdMrEvent
  )
{
  UINTN                     Index;
  UINT32                    DigestIndex;
  UINT32                    DigestCount;
  TPMI_ALG_HASH             HashAlgo;
  UINT32                    DigestSize;
  UINT8                     *DigestBuffer;
  UINT32                    EventSize;
  UINT8                     *EventBuffer;

  Print (L">>Event:\n");
  Print (L"    Mr Index  - %d\n", TdMrEvent->MrIndex);
  Print (L"    EventType - 0x%08x\n", TdMrEvent->EventType);
  Print (L"    DigestCount: 0x%08x\n", TdMrEvent->Digests.count);

  DigestCount = TdMrEvent->Digests.count;
  HashAlgo = TdMrEvent->Digests.digests[0].hashAlg;
  DigestBuffer = (UINT8 *)&TdMrEvent->Digests.digests[0].digest;
  for (DigestIndex = 0; DigestIndex < DigestCount; DigestIndex++) {
    Print (L"    HashAlgo : 0x%04x\n", HashAlgo);
    Print (L"    Digest(%d): ", DigestIndex);
    DigestSize = GetHashSizeByAlgo (HashAlgo);
    for (Index = 0; Index < DigestSize; Index++) {
      Print (L"%02x", DigestBuffer[Index]);
    }
    Print (L"\n");
    //
    // Prepare next
    //
    CopyMem (&HashAlgo, DigestBuffer + DigestSize, sizeof(TPMI_ALG_HASH));
    DigestBuffer = DigestBuffer + DigestSize + sizeof(TPMI_ALG_HASH);
  }
  DigestBuffer = DigestBuffer - sizeof(TPMI_ALG_HASH);

  CopyMem (&EventSize, DigestBuffer, sizeof(TdMrEvent->EventSize));
  Print (L"    EventSize - 0x%08x\n", EventSize);
  EventBuffer = DigestBuffer + sizeof(TdMrEvent->EventSize);
  ParseEventData (TdMrEvent->EventType, EventBuffer, EventSize);
  Print (L"\n");
}


UINTN
GetMrEventSize (
  IN TD_EVENT        *TdMrEvent
  )
{
  UINT32                    DigestIndex;
  UINT32                    DigestCount;
  TPMI_ALG_HASH             HashAlgo;
  UINT32                    DigestSize;
  UINT8                     *DigestBuffer;
  UINT32                    EventSize;
  UINT8                     *EventBuffer;

  DigestCount = TdMrEvent->Digests.count;
  HashAlgo = TdMrEvent->Digests.digests[0].hashAlg;
  DigestBuffer = (UINT8 *)&TdMrEvent->Digests.digests[0].digest;
  for (DigestIndex = 0; DigestIndex < DigestCount; DigestIndex++) {
    DigestSize = GetHashSizeByAlgo (HashAlgo);
    //
    // Prepare next
    //
    CopyMem (&HashAlgo, DigestBuffer + DigestSize, sizeof(TPMI_ALG_HASH));
    DigestBuffer = DigestBuffer + DigestSize + sizeof(TPMI_ALG_HASH);
  }
  DigestBuffer = DigestBuffer - sizeof(TPMI_ALG_HASH);

  CopyMem (&EventSize, DigestBuffer, sizeof(TdMrEvent->EventSize));
  EventBuffer = DigestBuffer + sizeof(TdMrEvent->EventSize);

  return (UINTN)EventBuffer + EventSize - (UINTN)TdMrEvent;
}

UINT8 *
GetDigestFromMrEvent (
  IN TD_EVENT            *TdMrEvent,
  IN TPMI_ALG_HASH             HashAlg
  )
{
  UINT32                    DigestIndex;
  UINT32                    DigestCount;
  TPMI_ALG_HASH             HashAlgo;
  UINT32                    DigestSize;
  UINT8                     *DigestBuffer;

  DigestCount = TdMrEvent->Digests.count;
  HashAlgo = TdMrEvent->Digests.digests[0].hashAlg;
  DigestBuffer = (UINT8 *)&TdMrEvent->Digests.digests[0].digest;
  for (DigestIndex = 0; DigestIndex < DigestCount; DigestIndex++) {
    DigestSize = GetHashSizeByAlgo (HashAlgo);

    if (HashAlg == HashAlgo) {
      return DigestBuffer;
    }

    //
    // Prepare next
    //
    CopyMem (&HashAlgo, DigestBuffer + DigestSize, sizeof(TPMI_ALG_HASH));
    DigestBuffer = DigestBuffer + DigestSize + sizeof(TPMI_ALG_HASH);
  }
  return NULL;
}

UINT32
GetTcgSpecIdNumberOfAlgorithms (
  IN TCG_EfiSpecIDEventStruct *TcgEfiSpecIdEventStruct
  )
{
  UINT32                           numberOfAlgorithms;

  CopyMem (&numberOfAlgorithms, TcgEfiSpecIdEventStruct + 1, sizeof(numberOfAlgorithms));
  return numberOfAlgorithms;
}

TCG_EfiSpecIdEventAlgorithmSize *
GetTcgSpecIdDigestSize (
  IN TCG_EfiSpecIDEventStruct *TcgEfiSpecIdEventStruct
  )
{
  return (TCG_EfiSpecIdEventAlgorithmSize *)((UINT8 *)TcgEfiSpecIdEventStruct + sizeof(*TcgEfiSpecIdEventStruct) + sizeof(UINT32));
}


/**
  This function dump event log for TDVF.

  @param[in]  EventLogFormat     The type of the event log for which the information is requested.
  @param[in]  EventLogLocation   A pointer to the memory address of the event log.
  @param[in]  EventLogLastEntry  If the Event Log contains more than one entry, this is a pointer to the
                                 address of the start of the last entry in the event log in memory.
  @param[in]  FinalEventsTable   A pointer to the memory address of the final event table.
**/
VOID
DumpTdxEventLog (
  IN EFI_TD_EVENT_LOG_FORMAT     EventLogFormat,
  IN EFI_PHYSICAL_ADDRESS        EventLogLocation,
  IN EFI_PHYSICAL_ADDRESS        EventLogLastEntry,
  IN EFI_TD_FINAL_EVENTS_TABLE   *FinalEventsTable,
  IN UINT32                      RegisterIndex,
  IN BOOLEAN                     CalculateExpected
  )
{
  TCG_PCR_EVENT_HDR                *TcgEventHdr;
  TD_EVENT                   *TdMrEvent;
  TCG_EfiSpecIDEventStruct         *TcgEfiSpecIdEventStruct;
  UINT32                           numberOfAlgorithms;
  TCG_EfiSpecIdEventAlgorithmSize  *digestSize;
  UINT8                            *DigestBuffer;
  TPMI_ALG_HASH                    HashAlg;
  UINTN                            NumberOfEvents;
  UINT32                           AlgoIndex;
  TPMU_HA                          HashDigest;
  TDREPORT_STRUCT                  *TdReportBuffer = NULL;
  UINT32                           TdReportBufferSize;
  UINT8                            *AdditionalData;
  UINT32                           DataSize=64;
  UINT8                            Index;



  Print (L"EventLogFormat: (0x%x)\n", EventLogFormat);
  Print (L"EventLogLocation: (0x%lx)\n", EventLogLocation);

  if (!CalculateExpected) {
    Print (L"TdEvent:\n");
    TcgEventHdr = (TCG_PCR_EVENT_HDR *)(UINTN)EventLogLocation;
    DumpTdvfEvent (TcgEventHdr);
    TcgEfiSpecIdEventStruct = (TCG_EfiSpecIDEventStruct *)(TcgEventHdr + 1);
    DumpTcgEfiSpecIdEventStruct (TcgEfiSpecIdEventStruct);

    TdMrEvent = (TD_EVENT *)((UINTN)TcgEfiSpecIdEventStruct + GetTcgEfiSpecIdEventStructSize (TcgEfiSpecIdEventStruct));
    while ((UINTN)TdMrEvent <= EventLogLastEntry) {
        if ((RegisterIndex == INDEX_ALL) || (RegisterIndex == TdMrEvent->MrIndex)) {
          DumpTdvfEvent2 (TdMrEvent);
        }
        TdMrEvent = (TD_EVENT *)((UINTN)TdMrEvent + GetMrEventSize (TdMrEvent));
      }

    if (FinalEventsTable == NULL) {
        Print (L"FinalEventsTable: NOT FOUND\n");
      } else {
        Print (L"FinalEventsTable:    (0x%x)\n", FinalEventsTable);
        Print (L"  Version:           (0x%x)\n", FinalEventsTable->Version);
        Print (L"  NumberOfEvents:    (0x%x)\n", FinalEventsTable->NumberOfEvents);

        TdMrEvent = (TD_EVENT *)(UINTN)(FinalEventsTable + 1);

        for (NumberOfEvents = 0; NumberOfEvents < FinalEventsTable->NumberOfEvents; NumberOfEvents++) {
          if ((RegisterIndex == INDEX_ALL) || (RegisterIndex == TdMrEvent->MrIndex)) {
            DumpTdvfEvent2 (TdMrEvent);
          }
          TdMrEvent = (TD_EVENT *)((UINTN)TdMrEvent + GetMrEventSize (TdMrEvent));
        }
      }
    Print (L"TdEvent end\n");
  } else {
    TcgEventHdr = (TCG_PCR_EVENT_HDR *)(UINTN)EventLogLocation;
    TcgEfiSpecIdEventStruct = (TCG_EfiSpecIDEventStruct *)(TcgEventHdr + 1);

    numberOfAlgorithms = GetTcgSpecIdNumberOfAlgorithms (TcgEfiSpecIdEventStruct);
    digestSize = GetTcgSpecIdDigestSize (TcgEfiSpecIdEventStruct);
    for (AlgoIndex = 0; AlgoIndex < numberOfAlgorithms; AlgoIndex++) {
      HashAlg = digestSize[AlgoIndex].algorithmId;
      ZeroMem (&HashDigest, sizeof(HashDigest));
      TdMrEvent = (TD_EVENT *)((UINTN)TcgEfiSpecIdEventStruct + GetTcgEfiSpecIdEventStructSize (TcgEfiSpecIdEventStruct));
      while ((UINTN)TdMrEvent <= EventLogLastEntry) {
        if ((RegisterIndex == TdMrEvent->MrIndex) && (TdMrEvent->EventType != EV_NO_ACTION)) {
          DigestBuffer = GetDigestFromMrEvent (TdMrEvent, HashAlg);
          if (DigestBuffer != NULL) {
            ExtendEvent (HashAlg, HashDigest.sha384, DigestBuffer);
          }
        }
          TdMrEvent = (TD_EVENT *)((UINTN)TdMrEvent + GetMrEventSize (TdMrEvent));
      }
      Print (L"TdEvent Calculated:\n");
      Print (L"    RegisterIndex  - %d\n", RegisterIndex);
      Print (L"    Digest    - ");
      for (Index = 0; Index < digestSize[AlgoIndex].digestSize; Index++) {
          Print (L"%02x", HashDigest.sha384[Index]);
        }
        Print (L"\n");
      TdReportBufferSize = sizeof(TDREPORT_STRUCT);
      TdReportBuffer = AllocatePool(TdReportBufferSize);
      AdditionalData = AllocatePool(DataSize);
      DumpRtmr((UINT8 *)TdReportBuffer, TdReportBufferSize, AdditionalData, DataSize);
      if (TdReportBuffer != NULL && RegisterIndex ==0)
        {
         Print (L"MRTD dumped:\n");
         Print (L"    MRTD  - %d\n", RegisterIndex);
         Print (L"    Digest    - ");
         InternalDumpData((UINT8 *)TdReportBuffer->Tdinfo.Mrtd, 0x30);
         Print (L"\n");
         }
         else
         {
         Print (L"RTMR Dumped:\n");
         Print (L"    RTMR[%d] \n", (RegisterIndex-1));
         Print (L"    Digest    - ");
         InternalDumpData((UINT8 *)TdReportBuffer->Tdinfo.Rtmrs[RegisterIndex-1], 0x30);
         Print (L"\n");
         FreePool(TdReportBuffer);
         TdReportBuffer = NULL;
         FreePool(AdditionalData);
         }
      }
    }
  }



#pragma pack(1)
typedef struct {
	EFI_ACPI_DESCRIPTION_HEADER Header;
	UINT32                      Rsv; // default to 0
	UINT64                      Laml;						   // Optional
	UINT64                      Lasa; 
} TDX_Event_Log_ACPI_Table; 
#pragma pack()

#define EFI_ACPI_6_1_TDX_EVENT_LOG_TABLE_SIGNATURE   SIGNATURE_32('T', 'D', 'E', 'L')

VOID
DumpAcpiTableHeader (
  EFI_ACPI_DESCRIPTION_HEADER                    *Header
  )
{
  UINT8               *Signature;
  UINT8               *OemTableId;
  UINT8               *CreatorId;
  
  Print (
    L"  Table Header:\n"
    );
  Signature = (UINT8*)&Header->Signature;
  Print (
    L"    Signature ............................................ '%c%c%c%c'\n",
    Signature[0],
    Signature[1],
    Signature[2],
    Signature[3]
    );
  Print (
    L"    Length ............................................... 0x%08x\n",
    Header->Length
    );
  Print (
    L"    Revision ............................................. 0x%02x\n",
    Header->Revision
    );
  Print (
    L"    Checksum ............................................. 0x%02x\n",
    Header->Checksum
    );
  Print (
    L"    OEMID ................................................ '%c%c%c%c%c%c'\n",
    Header->OemId[0],
    Header->OemId[1],
    Header->OemId[2],
    Header->OemId[3],
    Header->OemId[4],
    Header->OemId[5]
    );
  OemTableId = (UINT8 *)&Header->OemTableId;
  Print (
    L"    OEM Table ID ......................................... '%c%c%c%c%c%c%c%c'\n",
    OemTableId[0],
    OemTableId[1],
    OemTableId[2],
    OemTableId[3],
    OemTableId[4],
    OemTableId[5],
    OemTableId[6],
    OemTableId[7]
    );
  Print (
    L"    OEM Revision ......................................... 0x%08x\n",
    Header->OemRevision
    );
  CreatorId = (UINT8 *)&Header->CreatorId;
  Print (
    L"    Creator ID ........................................... '%c%c%c%c'\n",
    CreatorId[0],
    CreatorId[1],
    CreatorId[2],
    CreatorId[3]
    );
  Print (
    L"    Creator Revision ..................................... 0x%08x\n",
    Header->CreatorRevision
    );

  return;
}



VOID
DumpAcpiTdxEventLog (
  IN EFI_TD_EVENT_LOG_FORMAT     EventLogFormat,
  IN EFI_PHYSICAL_ADDRESS        EventLogLocation,
  IN UINT64                      Laml,
  IN UINT64                      RegisterIndex
  )
{
  TCG_PCR_EVENT_HDR                *TcgEventHdr;
  TD_EVENT                   *TdMrEvent;
  TCG_EfiSpecIDEventStruct         *TcgEfiSpecIdEventStruct;
  UINT32                           numberOfAlgorithms;
  TCG_EfiSpecIdEventAlgorithmSize  *digestSize;
  UINT8                            *DigestBuffer;
  TPMI_ALG_HASH                    HashAlg;
  UINT32                           AlgoIndex;
  TPMU_HA                          HashDigest;
  TDREPORT_STRUCT                  *TdReportBuffer = NULL;
  UINT32                           TdReportBufferSize;
  UINT8                            *AdditionalData;
  UINT32                           DataSize=64;
  UINT8                            Index;

  Print (L"EventLogFormat: (0x%x)\n", EventLogFormat);
  Print (L"EventLogLocation: (0x%lx)\n", EventLogLocation);
      TcgEventHdr = (TCG_PCR_EVENT_HDR *)(UINTN)EventLogLocation;
      DumpTdvfEvent (TcgEventHdr);
      TcgEfiSpecIdEventStruct = (TCG_EfiSpecIDEventStruct *)(TcgEventHdr + 1);
      DumpTcgEfiSpecIdEventStruct (TcgEfiSpecIdEventStruct);

      TdMrEvent = (TD_EVENT *)((UINTN)TcgEfiSpecIdEventStruct + GetTcgEfiSpecIdEventStructSize (TcgEfiSpecIdEventStruct));
      while ((UINTN)TdMrEvent <=(EventLogLocation+ (Laml-1)) && ((0 <= TdMrEvent->MrIndex) && (TdMrEvent->MrIndex) <=4) && (TdMrEvent->Digests.count ==1)) {
        if ((RegisterIndex == INDEX_ALL) || (RegisterIndex == TdMrEvent->MrIndex)) {
         // Print(L"Start Dump Td Event\n");
          DumpTdvfEvent2 (TdMrEvent);
         // Print(L"Finish Dump Td Event\n");

        }
        TdMrEvent = (TD_EVENT *)((UINTN)TdMrEvent + GetMrEventSize (TdMrEvent));
      }
    Print (L"TdEvent end\n");
      
    for (RegisterIndex = 0; RegisterIndex <= MAX_TDX_REG_INDEX; RegisterIndex++){
      TcgEventHdr = (TCG_PCR_EVENT_HDR *)(UINTN)EventLogLocation;
      TcgEfiSpecIdEventStruct = (TCG_EfiSpecIDEventStruct *)(TcgEventHdr + 1);

      numberOfAlgorithms = GetTcgSpecIdNumberOfAlgorithms (TcgEfiSpecIdEventStruct);
      digestSize = GetTcgSpecIdDigestSize (TcgEfiSpecIdEventStruct);
      for (AlgoIndex = 0; AlgoIndex < numberOfAlgorithms; AlgoIndex++) {
        HashAlg = digestSize[AlgoIndex].algorithmId;
        ZeroMem (&HashDigest, sizeof(HashDigest));

        TdMrEvent = (TD_EVENT *)((UINTN)TcgEfiSpecIdEventStruct + GetTcgEfiSpecIdEventStructSize (TcgEfiSpecIdEventStruct));
       while ((UINTN)TdMrEvent <=(EventLogLocation+ (Laml-1)) && ((0 <= TdMrEvent->MrIndex) && (TdMrEvent->MrIndex) <=4) && (TdMrEvent->Digests.count ==1)) {
          if ((RegisterIndex == TdMrEvent->MrIndex) && (TdMrEvent->EventType != EV_NO_ACTION)) {
            DigestBuffer = GetDigestFromMrEvent (TdMrEvent, HashAlg);
            if (DigestBuffer != NULL) {
              ExtendEvent (HashAlg, HashDigest.sha1, DigestBuffer);
            }
          }
          TdMrEvent = (TD_EVENT *)((UINTN)TdMrEvent + GetMrEventSize (TdMrEvent));
        }
        Print (L"TdEvent Calculated:\n");
        Print (L"    RegisterIndex  - %d\n", RegisterIndex);
        Print (L"    Digest    - ");
        for (Index = 0; Index < digestSize[AlgoIndex].digestSize; Index++) {
          Print (L"%02x", HashDigest.sha1[Index]);
        }
        Print (L"\n");

      }
      TdReportBufferSize = sizeof(TDREPORT_STRUCT);
      TdReportBuffer = AllocatePool(TdReportBufferSize);
      AdditionalData = AllocatePool(DataSize);
      DumpRtmr((UINT8 *)TdReportBuffer, TdReportBufferSize, AdditionalData, DataSize);
      if (TdReportBuffer != NULL && RegisterIndex ==0)
        {
         Print (L"MRTD dumped:\n");
         Print (L"    MRTD  - %d\n", RegisterIndex);
         Print (L"    Digest    - ");
         InternalDumpData((UINT8 *)TdReportBuffer->Tdinfo.Mrtd, 0x30);
         Print (L"\n");
         }
         else
         {
         Print (L"RTMR Dumped:\n");
         Print (L"    RTMR[%d] \n", (RegisterIndex-1));
         Print (L"    Digest    - ");
         InternalDumpData((UINT8 *)TdReportBuffer->Tdinfo.Rtmrs[RegisterIndex-1], 0x30);
         Print (L"\n");
         FreePool(TdReportBuffer);
         TdReportBuffer = NULL;
         FreePool(AdditionalData);
         }
    }
}

VOID
EFIAPI
DumpAcpiTdxEvent (
  VOID  *Table
  )

{
  TDX_Event_Log_ACPI_Table                       *Tdxl;

  Tdxl = Table;
  
  //
  // Dump TDXL table
  //
  Print (
    L"*****************************************************************************\n"
    L"*         TDX Event Log ACPI Table                                *\n"
    L"*****************************************************************************\n"
    );

  Print (
    L"Tdxl address ............................................. 0x%016lx\n",
    (UINT64)(UINTN)Tdxl
    );
  
  DumpAcpiTableHeader(&(Tdxl->Header));
  
  Print (
    L"  Table Contents:\n"
    );
  Print (
    L"    Reserved ................................................ 0x%08x\n",
    ((TDX_Event_Log_ACPI_Table *)Tdxl)->Rsv
    );
	
  Print (
    L"    Laml ................................................. 0x%08x\n",
     Tdxl->Laml
     );
  Print (
     L"    Lasa ................................................. 0x%08x\n",
     Tdxl->Lasa
     );

  Print (         
    L"*****************************************************************************\n\n"
    );
  DumpAcpiTdxEventLog(0x2,Tdxl->Lasa,Tdxl->Laml,0xFFFFFFFF);
  return;
}



VOID
DumpSelectAcpiTable (
  EFI_ACPI_DESCRIPTION_HEADER                    *Table
  )
{
  if (Table->Signature == EFI_ACPI_6_1_TDX_EVENT_LOG_TABLE_SIGNATURE){
    DumpAcpiTdxEvent(Table);
  }
}

EFI_ACPI_2_0_ROOT_SYSTEM_DESCRIPTION_POINTER *
ScanAcpiRSDP (
  VOID
  )
{
  UINTN                                                       Index;
  EFI_ACPI_2_0_ROOT_SYSTEM_DESCRIPTION_POINTER                *Rsdp;
  
  Rsdp = NULL;
  for (Index = 0; Index < gST->NumberOfTableEntries; Index ++) {
    if (CompareGuid (&gEfiAcpiTableGuid, &(gST->ConfigurationTable[Index].VendorGuid))) {
      Rsdp = gST->ConfigurationTable[Index].VendorTable;
      break;
    }
  }

  return Rsdp;
}

EFI_ACPI_DESCRIPTION_HEADER *
ScanAcpiRSDT (
  IN EFI_ACPI_2_0_ROOT_SYSTEM_DESCRIPTION_POINTER *Rsdp
  )
{
  return (EFI_ACPI_DESCRIPTION_HEADER*)((UINTN)Rsdp->RsdtAddress);    
}

EFI_ACPI_DESCRIPTION_HEADER *
ScanAcpiXSDT (
  IN EFI_ACPI_2_0_ROOT_SYSTEM_DESCRIPTION_POINTER *Rsdp
  )
{
  return (EFI_ACPI_DESCRIPTION_HEADER*)((UINTN)Rsdp->XsdtAddress);    
}

VOID
DumpAcpiTableWithSign (
  UINT32                                TableSign
  )
{
  EFI_ACPI_2_0_ROOT_SYSTEM_DESCRIPTION_POINTER   *Rsdp;
  EFI_ACPI_DESCRIPTION_HEADER                    *Rsdt;
  EFI_ACPI_DESCRIPTION_HEADER                    *Xsdt;
  EFI_ACPI_DESCRIPTION_HEADER                    *Table;
  UINTN                                          EntryCount;
  UINTN                                          Index;
  UINT32                                         *RsdtEntryPtr;
  UINT64                                         *XsdtEntryPtr;
  UINT64                                         TempEntry;

  //
  // Scan RSDP
  //
  Rsdp = ScanAcpiRSDP ();
  if (Rsdp == NULL) {
    return;
  }
  Print (L"Rsdp - 0x%x\n", Rsdp);

  //
  // Scan RSDT
  //
  Rsdt = ScanAcpiRSDT (Rsdp);
  Print (L"Rsdt - 0x%x\n", Rsdt);
  
  //
  // Scan XSDT
  //
  Xsdt = ScanAcpiXSDT (Rsdp);
  Print (L"Xsdt - 0x%x\n", Xsdt);
 
  //
  // Dump each table in RSDT
  //
  if ((Xsdt == NULL) && (Rsdt != NULL)) {
    EntryCount = (Rsdt->Length - sizeof (EFI_ACPI_DESCRIPTION_HEADER)) / 4;
    RsdtEntryPtr = (UINT32* )(UINTN)(Rsdt + 1);
    for (Index = 0; Index < EntryCount; Index ++, RsdtEntryPtr ++) {
      Table = (EFI_ACPI_DESCRIPTION_HEADER *)((UINTN)(*RsdtEntryPtr));
      if (Table == NULL) {
        continue;
      }
      Print (L"Table - 0x%x (0x%x)\n", Table, Table->Signature);
      if (Table->Signature == TableSign) {
        DumpSelectAcpiTable (Table);
      }
    }
  }
  
  //
  // Dump each table in XSDT
  //
  if (Xsdt != NULL) {
    EntryCount = (Xsdt->Length - sizeof (EFI_ACPI_DESCRIPTION_HEADER)) / 8;
    XsdtEntryPtr = (UINT64 *)(UINTN)(Xsdt + 1);
    CopyMem(&TempEntry, XsdtEntryPtr, sizeof(UINT64));
    for (Index = 0; Index < EntryCount; Index ++, XsdtEntryPtr ++) {
      CopyMem(&TempEntry, XsdtEntryPtr, sizeof(UINT64));
      Table = (EFI_ACPI_DESCRIPTION_HEADER *)((UINTN)TempEntry);
      if (Table == NULL) {
        continue;
      }
      Print (L"Table - 0x%x (0x%x)\n", Table, Table->Signature);
      if (Table->Signature == TableSign) {
        DumpSelectAcpiTable (Table);
      }
    }
  }
  
  return;
}

/**
  This function print usage.
**/
VOID
PrintUsage (
  VOID
  )
{
  Print (
    L"DumpTdxEventLog Version 0.1\n"
    L"Copyright (C) Intel Corp 2021. All rights reserved.\n"
    L"\n"
    );
  Print (
    L"DumpTdxEventLog in EFI Shell Environment.\n"
    L"\n"
    L"usage: DumpTdxEventLog [-I <Mr Index>] [-E]\n"
    L"usage: DumpTdxEventLog [-A]\n"
    L"usage: DumpTdxEventLog [-R]\n"
    );
  Print (
    L"  -I   - TD Registrer Index, the valid value is 0-5 (case sensitive)\n"
    L"  -E   - Print expected RTMR values and RTMR Values get from TdReport\n"
    L"  -A   - Dump Tdx Event log from the location in ACPI table, Print expected RTMR values and RTMR values get from TdReport\n"
    L"  -R   - Dump RTMR[0]-RTMR[3] & MRTD value from TdReport\n"
    );
  return;
}

/**
  The driver's entry point.

  @param[in] ImageHandle  The firmware allocated handle for the EFI image.  
  @param[in] SystemTable  A pointer to the EFI System Table.
  
  @retval EFI_SUCCESS     The entry point is executed successfully.
  @retval other           Some error occurs when executing this entry point.
**/
EFI_STATUS
EFIAPI
UefiMain (
  IN    EFI_HANDLE                  ImageHandle,
  IN    EFI_SYSTEM_TABLE            *SystemTable
  )
{
  EFI_STATUS                       Status;
  EFI_STATUS                       TdxTcg2Status;
  LIST_ENTRY                       *ParamPackage;
  CHAR16                           *IndexName;
  BOOLEAN                          CalculateExpected;
  EFI_TD_PROTOCOL                  *TdProtocol;
  EFI_PHYSICAL_ADDRESS             EventLogLocation;
  EFI_PHYSICAL_ADDRESS             EventLogLastEntry;
  BOOLEAN                          EventLogTruncated;
  UINT32                           Index;
  EFI_TD_BOOT_SERVICE_CAPABILITY   ProtocolCapability;
  EFI_TD_FINAL_EVENTS_TABLE        *FinalEventsTable;
  Status = ShellCommandLineParse (mParamList, &ParamPackage, NULL, TRUE);
  if (EFI_ERROR(Status)) {
    Print(L"ERROR: Incorrect command line.\n");
    return Status;
  }

  if (ParamPackage == NULL ||
     ShellCommandLineGetFlag(ParamPackage, L"-?") ||
     ShellCommandLineGetFlag(ParamPackage, L"-h")) {
    PrintUsage ();
    return EFI_SUCCESS;
  }

  //
  // Dump ACPI
  //
  if (ShellCommandLineGetFlag(ParamPackage, L"-A")) {
    DumpAcpiTableWithSign(EFI_ACPI_6_1_TDX_EVENT_LOG_TABLE_SIGNATURE);
    return EFI_SUCCESS;
  }
//
// Dump Raw RTMRs and MRTD
//
  if (ShellCommandLineGetFlag(ParamPackage, L"-R"))
  {
    UINT32                           RegisterIndex;
    TDREPORT_STRUCT                  *TdReportBuffer = NULL;
    UINT32                           TdReportBufferSize;
    UINT8                            *AdditionalData;
    UINT32                           DataSize=64;
    TdReportBufferSize = sizeof(TDREPORT_STRUCT);
    TdReportBuffer = AllocatePool(TdReportBufferSize);
    AdditionalData = AllocatePool(DataSize);
    DumpRtmr((UINT8 *)TdReportBuffer, TdReportBufferSize, AdditionalData, DataSize);
    for (RegisterIndex = 0; RegisterIndex <= MAX_TDX_REG_INDEX; RegisterIndex++)
    {
       if (TdReportBuffer != NULL && RegisterIndex ==0)
       {
         Print (L"MRTD dumped:\n");
         Print (L"    MRTD  - %d\n", RegisterIndex);
         Print (L"    Digest    - ");
         InternalDumpData((UINT8 *)TdReportBuffer->Tdinfo.Mrtd, 0x30);
         Print (L"\n");
         }else{
         Print (L"RTMR Dumped:\n");
         Print (L"    RTMR[%d] \n", (RegisterIndex-1));
         Print (L"    Digest    - ");
         InternalDumpData((UINT8 *)TdReportBuffer->Tdinfo.Rtmrs[RegisterIndex-1], 0x30);
         Print (L"\n");
         }
    }
    FreePool(TdReportBuffer);
    FreePool(AdditionalData);
    return EFI_SUCCESS;
  }

  //
  // Get Index
  //
  IndexName = (CHAR16 *)ShellCommandLineGetValue(ParamPackage, L"-I");
  if (IndexName == NULL) {
    Index = INDEX_ALL;
  } else {
    if (StrCmp (IndexName, L"ALL") == 0) {
      Index = INDEX_ALL;
    } else {
      Index = (UINT32)StrDecimalToUintn (IndexName);
      if (Index > MAX_TDX_REG_INDEX) {
        Print (L"ERROR: Mr Index too large (%d)!\n", Index);
        return EFI_NOT_FOUND;
      }
    }
  }
  Print(L"Parameter -I: MrIndex = 0x%x\n", Index);
  //
  // If we need calculate expected value
  //
  CalculateExpected = ShellCommandLineGetFlag(ParamPackage, L"-E");
  Print(L"Parameter -E: CalculateExpected = %d\n", CalculateExpected);


  //
  // Get Tcg2
  //
  TdxTcg2Status = gBS->LocateProtocol (&gEfiTdProtocolGuid, NULL, (VOID **) &TdProtocol);
  if (EFI_ERROR (TdxTcg2Status)) {
    Print (L"ERROR: Locate EfiTdProtocol - %r\n", TdxTcg2Status);
    return TdxTcg2Status;
  }else{
      Print (L"Locate EFI TdProtocol -%r\n", TdxTcg2Status);
    }

  ZeroMem (&ProtocolCapability, sizeof(ProtocolCapability));
  ProtocolCapability.Size = sizeof(ProtocolCapability);
  Status = TdProtocol->GetCapability (
                           TdProtocol,
                           &ProtocolCapability
                           );
  if (EFI_ERROR (Status)) {
    Print (L"ERROR: TdProtocol->GetCapability - %r\n", Status);
    return Status;
  }
  Status = TdProtocol->GetEventLog (
                               TdProtocol,
                               EFI_TD_EVENT_LOG_FORMAT_TCG_2,
                               &EventLogLocation,
                               &EventLogLastEntry,
                               &EventLogTruncated
                               );
  if (EFI_ERROR (Status)) {
    Print (L"ERROR: TdProtocol->GetEventLog(0x%x) - %r\n", EFI_TD_EVENT_LOG_FORMAT_TCG_2, Status);
    }
  if (EventLogTruncated) {
        Print (L"WARNING: EventLogTruncated\n");
    }

  FinalEventsTable = NULL;
  EfiGetSystemConfigurationTable (&gEfiTdFinalEventsTableGuid, (VOID **)&FinalEventsTable);


  //
  // DumpLog
  //
  if (TdxTcg2Status == EFI_SUCCESS){
    if (CalculateExpected && (Index == INDEX_ALL)) {
      for (Index = 0; Index <= MAX_TDX_REG_INDEX; Index++) {
        DumpTdxEventLog (EFI_TD_EVENT_LOG_FORMAT_TCG_2, EventLogLocation, EventLogLastEntry, FinalEventsTable, Index, CalculateExpected);
      }
    } else {
      DumpTdxEventLog (EFI_TD_EVENT_LOG_FORMAT_TCG_2, EventLogLocation, EventLogLastEntry, FinalEventsTable, Index, CalculateExpected);
    }
  }
  return EFI_SUCCESS;
}
