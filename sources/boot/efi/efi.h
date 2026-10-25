#ifndef NEWOS_EFI_H
#define NEWOS_EFI_H

// ==============================================================================
// NewOS V1 64-bit - Definizioni e Protocolli UEFI 2.x Freestanding
// (C) Newspoint Software - NewSoft - NewOS V1
// Nessuna dipendenza esterna da EDK2 o GNU-EFI
// ==============================================================================

#define EFIAPI __attribute__((ms_abi))

typedef unsigned char      UINT8;
typedef unsigned short     UINT16;
typedef unsigned int       UINT32;
typedef unsigned long long UINT64;
typedef signed char        INT8;
typedef signed short       INT16;
typedef signed int         INT32;
typedef signed long long   INT64;

typedef UINT64             UINTN;
typedef INT64              INTN;
typedef UINT16             CHAR16;
typedef UINT8              CHAR8;
typedef UINT8              BOOLEAN;
typedef void               VOID;

#define NULL  ((VOID*)0)
#define TRUE  1
#define FALSE 0

typedef UINTN              EFI_STATUS;
typedef VOID*              EFI_HANDLE;
typedef VOID*              EFI_EVENT;
typedef UINT64             EFI_PHYSICAL_ADDRESS;
typedef UINT64             EFI_VIRTUAL_ADDRESS;

#define EFI_SUCCESS               0ULL
#define EFI_LOAD_ERROR            (0x8000000000000000ULL | 1ULL)
#define EFI_INVALID_PARAMETER     (0x8000000000000000ULL | 2ULL)
#define EFI_UNSUPPORTED           (0x8000000000000000ULL | 3ULL)
#define EFI_BAD_BUFFER_SIZE       (0x8000000000000000ULL | 4ULL)
#define EFI_BUFFER_TOO_SMALL      (0x8000000000000000ULL | 5ULL)
#define EFI_NOT_READY             (0x8000000000000000ULL | 6ULL)
#define EFI_DEVICE_ERROR          (0x8000000000000000ULL | 7ULL)
#define EFI_WRITE_PROTECTED       (0x8000000000000000ULL | 8ULL)
#define EFI_OUT_OF_RESOURCES      (0x8000000000000000ULL | 9ULL)
#define EFI_VOLUME_CORRUPTED      (0x8000000000000000ULL | 10ULL)
#define EFI_VOLUME_FULL           (0x8000000000000000ULL | 11ULL)
#define EFI_NO_MEDIA              (0x8000000000000000ULL | 12ULL)
#define EFI_MEDIA_CHANGED         (0x8000000000000000ULL | 13ULL)
#define EFI_NOT_FOUND             (0x8000000000000000ULL | 14ULL)
#define EFI_ACCESS_DENIED         (0x8000000000000000ULL | 15ULL)

#define EFI_ERROR(status) (((INTN)(status)) < 0)

typedef struct {
    UINT32 Data1;
    UINT16 Data2;
    UINT16 Data3;
    UINT8  Data4[8];
} EFI_GUID;

typedef struct {
    UINT64 Signature;
    UINT32 Revision;
    UINT32 HeaderSize;
    UINT32 CRC32;
    UINT32 Reserved;
} EFI_TABLE_HEADER;

// Forward declarations
struct _EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL;
struct _EFI_SYSTEM_TABLE;
struct _EFI_BOOT_SERVICES;

// --- Simple Text Output Protocol ---
typedef EFI_STATUS (EFIAPI *EFI_TEXT_STRING)(
    struct _EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *This,
    const CHAR16                            *String
);

typedef EFI_STATUS (EFIAPI *EFI_TEXT_CLEAR_SCREEN)(
    struct _EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *This
);

typedef struct _EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL {
    VOID                 *Reset;
    EFI_TEXT_STRING      OutputString;
    VOID                 *TestString;
    VOID                 *QueryMode;
    VOID                 *SetMode;
    VOID                 *SetAttribute;
    EFI_TEXT_CLEAR_SCREEN ClearScreen;
    VOID                 *SetCursorPosition;
    VOID                 *EnableCursor;
    VOID                 *Mode;
} EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL;

// --- Memory Allocation ---
typedef enum {
    AllocateAnyPages,
    AllocateMaxAddress,
    AllocateAddress,
    MaxAllocateType
} EFI_ALLOCATE_TYPE;

typedef enum {
    EfiReservedMemoryType,
    EfiLoaderCode,
    EfiLoaderData,
    EfiBootServicesCode,
    EfiBootServicesData,
    EfiRuntimeServicesCode,
    EfiRuntimeServicesData,
    EfiConventionalMemory,
    EfiUnusableMemory,
    EfiACPIReclaimMemory,
    EfiACPIMemoryNVS,
    EfiMemoryMappedIO,
    EfiMemoryMappedIOPortSpace,
    EfiPalCode,
    EfiPersistentMemory,
    EfiMaxMemoryType
} EFI_MEMORY_TYPE;

typedef struct {
    UINT32               Type;
    EFI_PHYSICAL_ADDRESS PhysicalStart;
    EFI_VIRTUAL_ADDRESS  VirtualStart;
    UINT64               NumberOfPages;
    UINT64               Attribute;
} EFI_MEMORY_DESCRIPTOR;

// --- Boot Services Functions ---
typedef EFI_STATUS (EFIAPI *EFI_ALLOCATE_PAGES)(
    EFI_ALLOCATE_TYPE     Type,
    EFI_MEMORY_TYPE       MemoryType,
    UINTN                 Pages,
    EFI_PHYSICAL_ADDRESS *Memory
);

typedef EFI_STATUS (EFIAPI *EFI_FREE_PAGES)(
    EFI_PHYSICAL_ADDRESS Memory,
    UINTN                Pages
);

typedef EFI_STATUS (EFIAPI *EFI_GET_MEMORY_MAP)(
    UINTN                 *MemoryMapSize,
    EFI_MEMORY_DESCRIPTOR *MemoryMap,
    UINTN                 *MapKey,
    UINTN                 *DescriptorSize,
    UINT32                *DescriptorVersion
);

typedef EFI_STATUS (EFIAPI *EFI_ALLOCATE_POOL)(
    EFI_MEMORY_TYPE MemoryType,
    UINTN           Size,
    VOID          **Buffer
);

typedef EFI_STATUS (EFIAPI *EFI_FREE_POOL)(
    VOID *Buffer
);

typedef EFI_STATUS (EFIAPI *EFI_HANDLE_PROTOCOL)(
    EFI_HANDLE Handle,
    EFI_GUID  *Protocol,
    VOID     **Interface
);

typedef EFI_STATUS (EFIAPI *EFI_LOCATE_PROTOCOL)(
    EFI_GUID *Protocol,
    VOID     *Registration,
    VOID    **Interface
);

typedef EFI_STATUS (EFIAPI *EFI_EXIT_BOOT_SERVICES)(
    EFI_HANDLE ImageHandle,
    UINTN      MapKey
);

typedef struct _EFI_BOOT_SERVICES {
    EFI_TABLE_HEADER            Hdr;
    VOID                       *RaiseTPL;
    VOID                       *RestoreTPL;
    EFI_ALLOCATE_PAGES          AllocatePages;
    EFI_FREE_PAGES              FreePages;
    EFI_GET_MEMORY_MAP          GetMemoryMap;
    EFI_ALLOCATE_POOL           AllocatePool;
    EFI_FREE_POOL               FreePool;
    VOID                       *CreateEvent;
    VOID                       *SetTimer;
    VOID                       *WaitForEvent;
    VOID                       *SignalEvent;
    VOID                       *CloseEvent;
    VOID                       *CheckEvent;
    VOID                       *InstallProtocolInterface;
    VOID                       *ReinstallProtocolInterface;
    VOID                       *UninstallProtocolInterface;
    EFI_HANDLE_PROTOCOL         HandleProtocol;
    VOID                       *VoidReserved;
    VOID                       *RegisterProtocolNotify;
    VOID                       *LocateHandle;
    VOID                       *LocateDevicePath;
    VOID                       *InstallConfigurationTable;
    VOID                       *LoadImage;
    VOID                       *StartImage;
    VOID                       *Exit;
    VOID                       *UnloadImage;
    EFI_EXIT_BOOT_SERVICES      ExitBootServices;
    VOID                       *GetNextMonotonicCount;
    VOID                       *Stall;
    VOID                       *SetWatchdogTimer;
    VOID                       *ConnectController;
    VOID                       *DisconnectController;
    VOID                       *OpenProtocol;
    VOID                       *CloseProtocol;
    VOID                       *OpenProtocolInformation;
    VOID                       *ProtocolsPerHandle;
    VOID                       *LocateHandleBuffer;
    EFI_LOCATE_PROTOCOL         LocateProtocol;
} EFI_BOOT_SERVICES;

typedef struct _EFI_SYSTEM_TABLE {
    EFI_TABLE_HEADER                 Hdr;
    CHAR16                          *FirmwareVendor;
    UINT32                           FirmwareRevision;
    EFI_HANDLE                       ConsoleInHandle;
    VOID                            *ConIn;
    EFI_HANDLE                       ConsoleOutHandle;
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *ConOut;
    EFI_HANDLE                       StandardErrorHandle;
    VOID                            *StdErr;
    VOID                            *RuntimeServices;
    EFI_BOOT_SERVICES               *BootServices;
    UINTN                            NumberOfTableEntries;
    VOID                            *ConfigurationTable;
} EFI_SYSTEM_TABLE;

// --- Loaded Image Protocol ---
#define EFI_LOADED_IMAGE_PROTOCOL_GUID \
    { 0x5B1B31A1, 0x9562, 0x11D2, { 0x8E, 0x3F, 0x00, 0xA0, 0xC9, 0x69, 0x72, 0x3B } }

typedef struct {
    UINT32            Revision;
    EFI_HANDLE        ParentHandle;
    EFI_SYSTEM_TABLE *SystemTable;
    EFI_HANDLE        DeviceHandle;
    VOID             *FilePath;
    VOID             *Reserved;
    UINT32            LoadOptionsSize;
    VOID             *LoadOptions;
    VOID             *ImageBase;
    UINT64            ImageSize;
    EFI_MEMORY_TYPE   ImageCodeType;
    EFI_MEMORY_TYPE   ImageDataType;
    VOID             *Unload;
} EFI_LOADED_IMAGE_PROTOCOL;

// --- File System Protocols ---
#define EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID \
    { 0x964E5B22, 0x6459, 0x11D2, { 0x8E, 0x39, 0x00, 0xA0, 0xC9, 0x69, 0x72, 0x3B } }

#define EFI_FILE_INFO_ID \
    { 0x09576E92, 0x6D3F, 0x11D2, { 0x8E, 0x39, 0x00, 0xA0, 0xC9, 0x69, 0x72, 0x3B } }

#define EFI_FILE_MODE_READ   0x0000000000000001ULL
#define EFI_FILE_READ_ONLY   0x0000000000000001ULL
#define EFI_FILE_DIRECTORY   0x0000000000000010ULL

struct _EFI_FILE_PROTOCOL;

typedef EFI_STATUS (EFIAPI *EFI_FILE_OPEN)(
    struct _EFI_FILE_PROTOCOL  *This,
    struct _EFI_FILE_PROTOCOL **NewHandle,
    const CHAR16               *FileName,
    UINT64                      OpenMode,
    UINT64                      Attributes
);

typedef EFI_STATUS (EFIAPI *EFI_FILE_CLOSE)(
    struct _EFI_FILE_PROTOCOL *This
);

typedef EFI_STATUS (EFIAPI *EFI_FILE_READ)(
    struct _EFI_FILE_PROTOCOL *This,
    UINTN                     *BufferSize,
    VOID                      *Buffer
);

typedef EFI_STATUS (EFIAPI *EFI_FILE_GET_INFO)(
    struct _EFI_FILE_PROTOCOL *This,
    EFI_GUID                  *InformationType,
    UINTN                     *BufferSize,
    VOID                      *Buffer
);

typedef struct _EFI_FILE_PROTOCOL {
    UINT64              Revision;
    EFI_FILE_OPEN       Open;
    EFI_FILE_CLOSE      Close;
    VOID               *Delete;
    EFI_FILE_READ       Read;
    VOID               *Write;
    VOID               *GetPosition;
    VOID               *SetPosition;
    EFI_FILE_GET_INFO   GetInfo;
    VOID               *SetInfo;
    VOID               *Flush;
} EFI_FILE_PROTOCOL;

struct _EFI_SIMPLE_FILE_SYSTEM_PROTOCOL;

typedef EFI_STATUS (EFIAPI *EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_OPEN_VOLUME)(
    struct _EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *This,
    EFI_FILE_PROTOCOL                      **Root
);

typedef struct _EFI_SIMPLE_FILE_SYSTEM_PROTOCOL {
    UINT64                                      Revision;
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_OPEN_VOLUME OpenVolume;
} EFI_SIMPLE_FILE_SYSTEM_PROTOCOL;

typedef struct {
    UINT64 Size;
    UINT64 FileSize;
    UINT64 PhysicalSize;
    UINT64 CreateTime[2];
    UINT64 LastAccessTime[2];
    UINT64 ModificationTime[2];
    UINT64 Attribute;
    CHAR16 FileName[256];
} EFI_FILE_INFO;

// --- Graphics Output Protocol (GOP) ---
#define EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID \
    { 0x9042A9DE, 0x23DC, 0x4A38, { 0x96, 0xFB, 0x7A, 0xDE, 0xD0, 0x80, 0x51, 0x6A } }

typedef enum {
    PixelRedGreenBlueReserved8BitPerColor,
    PixelBlueGreenRedReserved8BitPerColor,
    PixelBitMask,
    PixelBltOnly,
    PixelFormatMax
} EFI_GRAPHICS_PIXEL_FORMAT;

typedef struct {
    UINT32                     Version;
    UINT32                     HorizontalResolution;
    UINT32                     VerticalResolution;
    EFI_GRAPHICS_PIXEL_FORMAT  PixelFormat;
    UINT32                     PixelInformation[4];
    UINT32                     PixelsPerScanLine;
} EFI_GRAPHICS_OUTPUT_MODE_INFORMATION;

typedef struct {
    UINT32                                MaxMode;
    UINT32                                Mode;
    EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *Info;
    UINTN                                 SizeOfInfo;
    EFI_PHYSICAL_ADDRESS                  FrameBufferBase;
    UINTN                                 FrameBufferSize;
} EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE;

struct _EFI_GRAPHICS_OUTPUT_PROTOCOL;

typedef EFI_STATUS (EFIAPI *EFI_GRAPHICS_OUTPUT_PROTOCOL_QUERY_MODE)(
    struct _EFI_GRAPHICS_OUTPUT_PROTOCOL  *This,
    UINT32                                 ModeNumber,
    UINTN                                 *SizeOfInfo,
    EFI_GRAPHICS_OUTPUT_MODE_INFORMATION **Info
);

typedef EFI_STATUS (EFIAPI *EFI_GRAPHICS_OUTPUT_PROTOCOL_SET_MODE)(
    struct _EFI_GRAPHICS_OUTPUT_PROTOCOL *This,
    UINT32                                ModeNumber
);

typedef struct _EFI_GRAPHICS_OUTPUT_PROTOCOL {
    EFI_GRAPHICS_OUTPUT_PROTOCOL_QUERY_MODE QueryMode;
    EFI_GRAPHICS_OUTPUT_PROTOCOL_SET_MODE   SetMode;
    VOID                                   *Blt;
    EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE      *Mode;
} EFI_GRAPHICS_OUTPUT_PROTOCOL;

// --- BootInfo Struct per il passaggio parametri a kernel_main ---
typedef struct {
    UINT64 framebuffer_base;
    UINT32 width;
    UINT32 height;
    UINT32 pitch;
    UINT32 bpp;
    UINT64 total_memory_bytes;
} __attribute__((packed)) BootInfo;

#endif // NEWOS_EFI_H
