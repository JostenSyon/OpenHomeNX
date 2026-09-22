// OpenHomeNX - shim di <switch.h> per build Linux/macOS.
// Fornisce i tipi, le macro e le funzioni libnx usate dal codice di OpenHomeNX
// come no-op o equivalenti POSIX, così i sorgenti originali compilano senza
// modifiche. Il compilatore trova QUESTO file prima di qualsiasi libnx reale
// mettendo questa directory in cima all'include path.
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <cstddef>
#include <string.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <pthread.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>

#ifdef __cplusplus
extern "C" {
#endif

// ---- tipi base ----
typedef uint8_t  u8;
typedef int8_t   s8;
typedef uint16_t u16;
typedef int16_t  s16;
typedef uint32_t u32;
typedef int32_t  s32;
typedef uint64_t u64;
typedef int64_t  s64;
typedef u32      Handle;
typedef u64      Result;

#define R_SUCCEEDED(res) ((res) == 0)
#define R_FAILED(res)    ((res) != 0)
#define R_MODULE(a) 0
#define R_DESCRIPTION(a) 0

// ---- FS (save mount) ----
typedef struct { u64 dummy; } FsFileSystem;
typedef struct { u64 application_id; u64 uid_lo, uid_hi; u32 save_data_type; u32 pad; } FsSaveDataAttribute;
typedef struct { u64 application_id; u64 uid_lo, uid_hi; u32 save_data_type; u32 pad; u64 size; u64 timestamp; } FsSaveDataInfo;
typedef struct { void* impl; } FsSaveDataInfoReader;
enum { FsSaveDataSpaceId_User = 0, FsSaveDataType_Account = 1 };

// ---- account (profili) ----
typedef struct { u64 uid[2]; } AccountUid;
typedef struct { u32 dummy; } AccountProfile;
typedef struct { char nickname[65]; char username[33]; } AccountProfileBase;
enum { AccountServiceType_Administrator = 1 };

// ---- applet / hook ----
typedef enum { AppletType_Application = 0, AppletType_SystemApplication = 1, AppletType_LibraryApplet = 2, AppletType_OverlayApplet = 3, AppletType_SystemApplet = 4 } AppletType;
typedef enum { AppletHookType_OnExitRequest = 0 } AppletHookType;
typedef struct { u32 dummy; } AppletHookCookie;

// ---- pl (shared fonts) ----
typedef enum {
    PlSharedFontType_Standard = 0, PlSharedFontType_ChineseSimplified = 1,
    PlSharedFontType_ChineseTraditional = 2, PlSharedFontType_KO = 3,
    PlSharedFontType_NintendoExt = 4
} PlSharedFontType;
typedef enum { PlServiceType_System = 0 } PlServiceType;
typedef struct { void* address; u32 size; } PlFontData;

// ---- hidsys (led) ----
typedef struct { u64 dummy; } HidsysUniquePadId;
typedef struct { u8 dummy[64]; } HidsysNotificationLedPattern;
typedef struct { u64 dummy; } GpioSession;
typedef struct { u64 dummy; } GpioPadSession;
enum { GpioDirection_Output = 0, GpioValue_Low = 0, GpioValue_High = 1 };
enum { SetSysProductModel_Invalid = 0, SetSysProductModel_Hoag = 1 };
enum { LITE_GPIO_ACCESS_MODE = 0, LITE_GPIO_DEVICE_CODE = 0 };
typedef enum { HidNpadIdType_No1=0, HidNpadIdType_No2, HidNpadIdType_No3, HidNpadIdType_No4, HidNpadIdType_No5, HidNpadIdType_No6, HidNpadIdType_No7, HidNpadIdType_Handheld } HidNpadIdType;

// ---- swkbd (tastiera virtuale) ----
typedef struct { u32 dummy; } SwkbdConfig;
enum { SwkbdType_Normal = 0, SwkbdType_NumPad = 1 };

// ---- ncm (forwarder) ----
enum { NcmStorageId_SdCard = 3 };
typedef enum { NsApplicationControlSource_CacheOnly = 0, NsApplicationControlSource_Storage = 1, NsApplicationControlSource_StorageOnly = 2 } NsApplicationControlSource;

// ---- settime / setlang ----
typedef enum { SetLanguage_JA=0, SetLanguage_ENGB, SetLanguage_FR, SetLanguage_FRCA, SetLanguage_DE, SetLanguage_IT, SetLanguage_ES, SetLanguage_ES419, SetLanguage_NL, SetLanguage_PT, SetLanguage_PTBR, SetLanguage_RU, SetLanguage_KO, SetLanguage_ZHTW, SetLanguage_ZHCN } SetLanguage;

// ---- thread libnx (nomi custom usati in remote_sync/autoupdate) ----
// Su Linux i thread sono pthread reali (autoupdate/remote_sync worker
// girano davvero in background, come su Switch).
typedef struct { void (*fn)(void*); void* arg; pthread_t th; int started; } Thread;
typedef struct { u64 dummy; } Mutex;
typedef struct { u64 dummy; } CondVar;

// ---- nifm (network interface management) ----
// Solo i nomi usati dal codice; i valori contano solo per coerenza interna
// su Linux (mai confrontati con libnx reale).
typedef enum { NifmServiceType_User = 0 } NifmServiceType;
typedef enum { NifmInternetConnectionType_Unknown = 0, NifmInternetConnectionType_WiFi = 1, NifmInternetConnectionType_Ethernet = 2 } NifmInternetConnectionType;
typedef enum { NifmInternetConnectionStatus_Unknown = 0, NifmInternetConnectionStatus_Connected = 1 } NifmInternetConnectionStatus;

// ===================== funzioni no-op / POSIX =====================
// romfs
static inline Result romfsInit(void) { return 0; }
static inline Result romfsExit(void) { return 0; }
// socket: su Linux i socket funzionano sempre, nessuna init richiesta
static inline Result socketInitializeDefault(void) { return 0; }
static inline Result socketExit(void) { return 0; }
// nifm: implementazioni Linux reali (link status via getifaddrs)
static inline Result nifmExit(void) { return 0; }
static inline Result nifmInitialize(NifmServiceType t) { (void)t; return 0; }
static inline Result nifmGetCurrentIpAddress(u32* out) {
    struct ifaddrs* list = NULL;
    if (getifaddrs(&list) != 0) return 1;
    u32 ip = 0;
    for (struct ifaddrs* it = list; it; it = it->ifa_next) {
        if (!it->ifa_addr || !it->ifa_name) continue;
        if (it->ifa_addr->sa_family != AF_INET) continue;
        if (it->ifa_flags & IFF_LOOPBACK) continue;
        if (!(it->ifa_flags & IFF_UP)) continue;
        ip = ((struct sockaddr_in*)it->ifa_addr)->sin_addr.s_addr;
        break;
    }
    freeifaddrs(list);
    if (!ip) return 1;
    if (out) *out = ip;
    return 0;
}
static inline Result nifmGetInternetConnectionStatus(NifmInternetConnectionType* type, u32* strength, NifmInternetConnectionStatus* st) {
    struct ifaddrs* list = NULL;
    if (getifaddrs(&list) != 0) return 1;
    const char* ifname = NULL;
    for (struct ifaddrs* it = list; it; it = it->ifa_next) {
        if (!it->ifa_addr || !it->ifa_name) continue;
        if (it->ifa_addr->sa_family != AF_INET) continue;
        if (it->ifa_flags & IFF_LOOPBACK) continue;
        if (!(it->ifa_flags & IFF_UP)) continue;
        u32 ip = ((struct sockaddr_in*)it->ifa_addr)->sin_addr.s_addr;
        if (!ip) continue;
        ifname = it->ifa_name;
        break;
    }
    freeifaddrs(list);
    if (!ifname) return 1;
    if (type) {
        if ((ifname[0] == 'e' && ifname[1] == 't' && ifname[2] == 'h') ||
            (ifname[0] == 'e' && ifname[1] == 'n'))
            *type = NifmInternetConnectionType_Ethernet;
        else
            *type = NifmInternetConnectionType_WiFi;
    }
    if (strength) *strength = 3;
    if (st) *st = NifmInternetConnectionStatus_Connected;
    return 0;
}
// applet hook
static inline void appletHook(AppletHookCookie* c, void (*fn)(AppletHookType, void*), void* p) {}
static inline void appletUnhook(AppletHookCookie* c) {}
static inline AppletType appletGetAppletType(void) { return AppletType_Application; }
static inline Result appletRequestLaunchApplication(u64 titleId, void* p) { return 1; }
// pl shared fonts
static inline Result plInitialize(PlServiceType st) { return 0; }
static inline Result plExit(void) { return 0; }
static inline Result plGetSharedFontByType(PlFontData* out, PlSharedFontType type) {
    if (out) { out->address = 0; out->size = 0; }
    return 1; // nessun font di sistema -> usa TTF locale
}
// set language
static inline Result setInitialize(void) { return 0; }
static inline Result setExit(void) { return 0; }
static inline Result setGetSystemLanguage(u64* out) { *out = 0; return 0; }
static inline void setMakeLanguage(u64 code, SetLanguage* out) { *out = SetLanguage_ENGB; }
// account
static inline Result accountInitialize(u32 type) { return 0; }
static inline Result accountExit(void) { return 0; }
static inline Result accountListAllUsers(AccountUid* out, s32 max, s32* total) { *total = 0; return 0; }
static inline Result accountGetProfile(AccountProfile* p, AccountUid uid) { return 1; }
static inline Result accountProfileGet(AccountProfile* p, void* img, AccountProfileBase* base) { return 1; }
static inline Result accountProfileClose(AccountProfile* p) { return 0; }
static inline Result accountProfileGetImageSize(AccountProfile* p, u32* sz) { return 1; }
static inline Result accountProfileLoadImage(AccountProfile* p, void* buf, u32 sz, u32* out) { return 1; }
// fs save mount (no-op: su Linux non si monta save di sistema)
static inline Result fsOpenSaveDataInfoReader(FsSaveDataInfoReader* r, u32 space) { return 1; }
static inline Result fsSaveDataInfoReaderRead(FsSaveDataInfoReader* r, FsSaveDataInfo* out, s64 n, s64* read) { *read = 0; return 0; }
static inline void fsSaveDataInfoReaderClose(FsSaveDataInfoReader* r) {}
static inline Result fsOpenSaveDataFileSystem(FsFileSystem* fs, u32 space, FsSaveDataAttribute* attr) { return 1; }
static inline void fsFsClose(FsFileSystem* fs) {}
static inline int fsdevMountDevice(const char* name, FsFileSystem fs) { return -1; }
static inline void fsdevUnmountDevice(const char* name) {}
static inline int fsdevCommitDevice(const char* name) { return -1; }
// ns
#define NACP_STRUCT_SIZE 0x4000
#define NACP_ICON_SIZE   0x20000
typedef struct { u8 data[NACP_STRUCT_SIZE]; } NacpStruct;
typedef struct { u8 nacp[NACP_STRUCT_SIZE]; u8 icon[NACP_ICON_SIZE]; } NsApplicationControlData;
static inline Result nsInitialize(void) { return 0; }
static inline Result nsExit(void) { return 0; }
static inline Result nsListApplicationRecord(void* recs, s32 n, s32 off, s32* cnt) { *cnt = 0; return 0; }
static inline Result nsGetApplicationControlData(NsApplicationControlSource src, u64 titleId, NsApplicationControlData* buf, s64 sz, u64* written) { (void)src; (void)titleId; (void)buf; (void)sz; *written = 0; return 1; }
// env next load (chainload -> su Linux gestito da stub emulator)
static inline bool envHasNextLoad(void) { return false; }
static inline void envSetNextLoad(const char* target, const char* args) {}
// svc / tick
static inline void svcSleepThread(s64 ns) { usleep((useconds_t)(ns / 1000)); }
static inline u64 armGetSystemTick(void) { struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); return (u64)ts.tv_sec * 1000000000ULL + (u64)ts.tv_nsec; }
static inline u64 armTicksToNs(u64 ticks) { return ticks; }
// thread libnx -> pthread reali
static void* nxThreadEntry(void* p) {
    Thread* t = (Thread*)p;
    if (t && t->fn) t->fn(t->arg);
    return NULL;
}
static inline Result threadCreate(Thread* t, void (*fn)(void*), void* arg, void* stack, s64 size, s32 prio, s32 id) {
    (void)stack; (void)size; (void)prio; (void)id;
    if (!t || !fn) return 1;
    t->fn = fn; t->arg = arg; t->started = 0;
    return 0;
}
static inline Result threadStart(Thread* t) {
    if (!t || !t->fn || t->started) return 1;
    if (pthread_create(&t->th, NULL, nxThreadEntry, t) != 0) return 1;
    t->started = 1;
    return 0;
}
static inline Result threadWaitForExit(Thread* t) {
    if (!t || !t->started) return 0;
    pthread_join(t->th, NULL);
    t->started = 0;
    return 0;
}
static inline Result threadClose(Thread* t) { (void)t; return 0; }
// swkbd (no-op -> fallisce, il codice usa showConfirmDialog fallback? no: gestito in ui)
static inline Result swkbdCreate(SwkbdConfig* cfg, u32 type) { (void)cfg; (void)type; return 0; }
static inline void swkbdConfigMakePresetDefault(SwkbdConfig* cfg) { (void)cfg; }
static inline Result swkbdShow(SwkbdConfig* cfg, char* out, size_t len) { (void)cfg; (void)out; (void)len; return 1; }
static inline void swkbdClose(SwkbdConfig* cfg) { (void)cfg; }
static inline void swkbdConfigSetStringLenMax(SwkbdConfig* cfg, u32 v) { (void)cfg; (void)v; }
static inline void swkbdConfigSetHeaderText(SwkbdConfig* cfg, const char* v) { (void)cfg; (void)v; }
static inline void swkbdConfigSetInitialText(SwkbdConfig* cfg, const char* v) { (void)cfg; (void)v; }
static inline void swkbdConfigSetType(SwkbdConfig* cfg, u32 v) { (void)cfg; (void)v; }
// UsbHsFs stubs (mai chiamati: OH_USB_UPDATE non definito nel build Linux)
typedef struct { u64 dummy; } UsbHsFsDevice;
static inline u32 usbHsFsGetPhysicalDeviceCount(void) { return 0; }
static inline u32 usbHsFsGetMountedDeviceCount(void) { return 0; }
static inline u32 usbHsFsListMountedDevices(UsbHsFsDevice* d, u32 n) { return 0; }
static inline bool usbHsFsUnmountDevice(UsbHsFsDevice* d, bool force) { return false; }

#ifdef __cplusplus
}
#endif