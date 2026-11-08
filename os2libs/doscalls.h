/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */

#ifndef _INCL_DOSCALLS_H_
#define _INCL_DOSCALLS_H_

#include "os2libs/os2types.h"

#ifdef __cplusplus
extern "C"
{
#endif

#define CCHMAXPATH 260
#define CCHMAXPATHCOMP 256

    enum
    {
        EXIT_THREAD,
        EXIT_PROCESS
    };

    enum
    {
        QSV_MAX_PATH_LENGTH = 1,
        QSV_MAX_TEXT_SESSIONS,
        QSV_MAX_PM_SESSIONS,
        QSV_MAX_VDM_SESSIONS,
        QSV_BOOT_DRIVE,
        QSV_DYN_PRI_VARIATION,
        QSV_MAX_WAIT,
        QSV_MIN_SLICE,
        QSV_MAX_SLICE,
        QSV_PAGE_SIZE,
        QSV_VERSION_MAJOR,
        QSV_VERSION_MINOR,
        QSV_VERSION_REVISION,
        QSV_MS_COUNT,
        QSV_TIME_LOW,
        QSV_TIME_HIGH,
        QSV_TOTPHYSMEM,
        QSV_TOTRESMEM,
        QSV_TOTAVAILMEM,
        QSV_MAXPRMEM,
        QSV_MAXSHMEM,
        QSV_TIMER_INTERVAL,
        QSV_MAX_COMP_LENGTH,
        QSV_FOREGROUND_FS_SESSION,
        QSV_FOREGROUND_PROCESS
    };

#pragma pack(push, 1)

    typedef struct TIB2
    {
        ULONG tib2_ultid;
        ULONG tib2_ulpri;
        ULONG tib2_version;
        USHORT tib2_usMCCount;
        USHORT tib2_fMCForceFlag;
    } TIB2, *PTIB2;

    typedef struct TIB
    {
        PVOID tib_pexchain;
        PVOID tib_pstack;
        PVOID tib_pstacklimit;
        PTIB2 tib_ptib2;
        ULONG tib_version;
        ULONG tib_ordinal;
    } TIB, *PTIB;

#pragma pack(pop)

    typedef struct
    {
        LONG lOffset;
        LONG lRange;
    } FILELOCK, *PFILELOCK;

    typedef struct PIB
    {
        ULONG pib_ulpid;
        ULONG pib_ulppid;
        HMODULE pib_hmte;
        PCHAR pib_pchcmd;
        PCHAR pib_pchenv;
        ULONG pib_flstatus;
        ULONG pib_ultype;
    } PIB, *PPIB;

    typedef VOID APIENTRY FNEXITLIST(ULONG);
    typedef FNEXITLIST *PFNEXITLIST;

    enum
    {
        TC_EXIT,
        TC_HARDERROR,
        TC_TRAP,
        TC_KILLPROCESS,
        TC_EXCEPTION
    };

    enum
    {
        EXLST_ADD = 1,
        EXLST_REMOVE,
        EXLST_EXIT
    };

    typedef void *PEXCEPTIONREGISTRATIONRECORD;
    typedef void *PEXCEPTIONREPORTRECORD;

    typedef struct
    {
        ULONG country;
        ULONG codepage;
    } COUNTRYCODE, *PCOUNTRYCODE;

    typedef struct
    {
        USHORT country;
        USHORT codepage;
    } COUNTRYCODE16, *PCOUNTRYCODE16;

    typedef struct
    {
        UCHAR hours;
        UCHAR minutes;
        UCHAR seconds;
        UCHAR hundredths;
        UCHAR day;
        UCHAR month;
        USHORT year;
        SHORT timezone;
        UCHAR weekday;
    } DATETIME, *PDATETIME;

    /* OS/2 Toolkit BSEMEMF memory permissions and allocation attributes. */
    enum
    {
        PAG_READ = 0x01,
        PAG_WRITE = 0x02,
        PAG_EXECUTE = 0x04,
        PAG_GUARD = 0x08,
        PAG_COMMIT = 0x10,
        PAG_DECOMMIT = 0x20,
        OBJ_TILE = 0x40,
        OBJ_GETTABLE = 0x100,
        OBJ_GIVEABLE = 0x200,
        PAG_DEFAULT = 0x400
    };

    typedef void *PEAOP2;

    enum
    {
        FILE_EXISTED = 1,
        FILE_CREATED,
        FILE_TRUNCATED
    };

    enum
    {
        FILE_OPEN = 0x01,
        FILE_TRUNCATE = 0x02,
        FILE_CREATE = 0x10
    };

    enum
    {
        FILE_NORMAL = 0x00,
        FILE_READONLY = 0x01,
        FILE_HIDDEN = 0x02,
        FILE_SYSTEM = 0x04,
        FILE_DIRECTORY = 0x10,
        FILE_ARCHIVED = 0x20
    };

    enum
    {
        OPEN_ACTION_FAIL_IF_EXISTS = 0x0000,
        OPEN_ACTION_OPEN_IF_EXISTS = 0x0001,
        OPEN_ACTION_REPLACE_IF_EXISTS = 0x0002,
        OPEN_ACTION_FAIL_IF_NEW = 0x0000,
        OPEN_ACTION_CREATE_IF_NEW = 0x0010,
    };

    enum
    {
        OPEN_ACCESS_READONLY = 0x0000,
        OPEN_ACCESS_WRITEONLY = 0x0001,
        OPEN_ACCESS_READWRITE = 0x0002,
        OPEN_SHARE_DENYREADWRITE = 0x0010,
        OPEN_SHARE_DENYWRITE = 0x0020,
        OPEN_SHARE_DENYREAD = 0x0030,
        OPEN_SHARE_DENYNONE = 0x0040,
        OPEN_FLAGS_NOINHERIT = 0x0080,
        OPEN_FLAGS_NO_LOCALITY = 0x0000,
        OPEN_FLAGS_SEQUENTIAL = 0x0100,
        OPEN_FLAGS_RANDOM = 0x0200,
        OPEN_FLAGS_RANDOMSEQUENTIAL = 0x0300,
        OPEN_FLAGS_NO_CACHE = 0x1000,
        OPEN_FLAGS_FAIL_ON_ERROR = 0x2000,
        OPEN_FLAGS_WRITE_THROUGH = 0x4000,
        OPEN_FLAGS_DASD = 0x8000,
        OPEN_FLAGS_NONSPOOLED = 0x00040000,
        OPEN_FLAGS_PROTECTED_HANDLE = 0x40000000
    };

    enum
    {
        FILE_BEGIN,
        FILE_CURRENT,
        FILE_END
    };

    enum
    {
        FIL_STANDARD = 1,
        FIL_QUERYEASIZE = 2,
        FIL_QUERYEASFROMLIST = 3,
        FIL_QUERYFULLNAME = 5,
        FIL_STANDARDL = 11,
        FIL_QUERYEASIZEL = 12
    };

#pragma pack(push, 1)

    typedef struct
    {
        USHORT day : 5;
        USHORT month : 4;
        USHORT year : 7;
    } FDATE, *PFDATE;

    typedef struct
    {
        USHORT twosecs : 5;
        USHORT minutes : 6;
        USHORT hours : 5;
    } FTIME, *PFTIME;

#pragma pack(pop)

    typedef struct
    {
        FDATE fdateCreation;
        FTIME ftimeCreation;
        FDATE fdateLastAccess;
        FTIME ftimeLastAccess;
        FDATE fdateLastWrite;
        FTIME ftimeLastWrite;
        ULONG cbFile;
        ULONG cbFileAlloc;
        ULONG attrFile;
    } FILESTATUS3, *PFILESTATUS3;

    typedef struct
    {
        FDATE fdateCreation;
        FTIME ftimeCreation;
        FDATE fdateLastAccess;
        FTIME ftimeLastAccess;
        FDATE fdateLastWrite;
        FTIME ftimeLastWrite;
        ULONG cbFile;
        ULONG cbFileAlloc;
        ULONG attrFile;
        ULONG cbList;
    } FILESTATUS4, *PFILESTATUS4;

    enum
    {
        CREATE_READY,
        CREATE_SUSPENDED
    };

    enum
    {
        STACK_SPARSE = 0,
        STACK_COMMITTED = 2
    };

    typedef VOID APIENTRY FNTHREAD(ULONG);
    typedef FNTHREAD *PFNTHREAD;

    typedef struct
    {
        ULONG codeTerminate;
        ULONG codeResult;
    } RESULTCODES, *PRESULTCODES;

    typedef struct
    {
        USHORT codeTerminate;
        USHORT codeResult;
    } RESULTCODES16, *PRESULTCODES16;

    enum
    {
        EXEC_SYNC,
        EXEC_ASYNC,
        EXEC_ASYNCRESULT,
        EXEC_TRACE,
        EXEC_BACKGROUND,
        EXEC_LOAD,
        EXEC_ASYNCRESULTDB
    };

    enum
    {
        DC_SEM_SHARED = 1
    };

    enum
    {
        DCMW_WAIT_ANY = (1 << 1),
        DCMW_WAIT_ALL = (1 << 2)
    };

    enum
    {
        SEM_INDEFINITE_WAIT = -1,
        SEM_IMMEDIATE_RETURN = 0
    };

    enum
    {
        DCWW_WAIT,
        DCWW_NOWAIT
    };

    enum
    {
        DCWA_PROCESS,
        DCWA_PROCESSTREE
    };

    enum
    {
        DSPI_WRTTHRU = 0x10,
    };

    enum
    {
        HDIR_CREATE = -1,
        HDIR_SYSTEM = 1
    };

    enum
    {
        MUST_HAVE_READONLY = ((FILE_READONLY << 8) | FILE_READONLY),
        MUST_HAVE_HIDDEN = ((FILE_HIDDEN << 8) | FILE_HIDDEN),
        MUST_HAVE_SYSTEM = ((FILE_SYSTEM << 8) | FILE_SYSTEM),
        MUST_HAVE_DIRECTORY = ((FILE_DIRECTORY << 8) | FILE_DIRECTORY),
        MUST_HAVE_ARCHIVED = ((FILE_ARCHIVED << 8) | FILE_ARCHIVED)
    };

    typedef struct
    {
        FDATE fdateCreation;
        FTIME ftimeCreation;
        FDATE fdateLastAccess;
        FTIME ftimeLastAccess;
        FDATE fdateLastWrite;
        FTIME ftimeLastWrite;
        ULONG cbFile;
        ULONG cbFileAlloc;
        USHORT attrFile;
        UCHAR cchName;
        CHAR achName[CCHMAXPATHCOMP];
    } FILEFINDBUF, *PFILEFINDBUF;

    typedef struct
    {
        ULONG oNextEntryOffset;
        FDATE fdateCreation;
        FTIME ftimeCreation;
        FDATE fdateLastAccess;
        FTIME ftimeLastAccess;
        FDATE fdateLastWrite;
        FTIME ftimeLastWrite;
        ULONG cbFile;
        ULONG cbFileAlloc;
        ULONG attrFile;
        UCHAR cchName;
        CHAR achName[CCHMAXPATHCOMP];
    } FILEFINDBUF3, *PFILEFINDBUF3;

    typedef struct
    {
        ULONG oNextEntryOffset;
        FDATE fdateCreation;
        FTIME ftimeCreation;
        FDATE fdateLastAccess;
        FTIME ftimeLastAccess;
        FDATE fdateLastWrite;
        FTIME ftimeLastWrite;
        ULONG cbFile;
        ULONG cbFileAlloc;
        ULONG attrFile;
        ULONG cbList;
        UCHAR cchName;
        CHAR achName[CCHMAXPATHCOMP];
    } FILEFINDBUF4, *PFILEFINDBUF4;

    typedef struct
    {
        USHORT iType;
        USHORT cbName;
        USHORT cbFSDName;
        USHORT cbFSAData;
        UCHAR szName[1];
        UCHAR szFSDName[1];
        UCHAR rgFSAData[1];
    } FSQBUFFER2, *PFSQBUFFER2;

    typedef struct
    {
        ULONG time;
        ULONG msecs;
        UCHAR hour;
        UCHAR minutes;
        UCHAR seconds;
        UCHAR hundredths;
        USHORT timezone;
        USHORT cusecTimerInterval;
        UCHAR day;
        UCHAR month;
        USHORT year;
        UCHAR weekday;
        UCHAR uchMajorVersion;
        UCHAR uchMinorVersion;
        UCHAR chRevisionLetter;
        UCHAR sgCurrent;
        UCHAR sgMax;
        UCHAR cHugeShift;
        UCHAR fProtectModeOnly;
        USHORT pidForeground;
        UCHAR fDynamicSched;
        UCHAR csecMaxWait;
        USHORT cmsecMinSlice;
        USHORT cmsecMaxSlice;
        USHORT bootdrive;
        UCHAR amecRAS[32];
        UCHAR csgWindowableVioMax;
        UCHAR csgPMMax;
    } GINFOSEG;

    typedef struct
    {
        PID pidCurrent;
        PID pidParent;
        USHORT prtyCurrent;
        TID tidCurrent;
        USHORT sgCurrent;
        UCHAR rfProcStatus;
        UCHAR dummy1;
        BOOL fForeground;
        UCHAR typeProcess;
        UCHAR dummy2;
        SEL selEnvironment;
        USHORT offCmdLine;
        USHORT cbDataSegment;
        USHORT cbStack;
        USHORT cbHeap;
        HMODULE hmod;
        SEL selDS;
    } LINFOSEG;

    enum
    {
        DEVINFO_PRINTER,
        DEVINFO_RS232,
        DEVINFO_FLOPPY,
        DEVINFO_COPROCESSOR,
        DEVINFO_SUBMODEL,
        DEVINFO_MODEL,
        DEVINFO_ADAPTER
    };

    enum
    {
        FAPPTYP_NOTSPEC = 0x0000,
        FAPPTYP_NOTWINDOWCOMPAT = 0x0001,
        FAPPTYP_WINDOWCOMPAT = 0x0002,
        FAPPTYP_WINDOWAPI = 0x0003,
        FAPPTYP_BOUND = 0x0008,
        FAPPTYP_DLL = 0x0010,
        FAPPTYP_DOS = 0x0020,
        FAPPTYP_PHYSDRV = 0x0040,
        FAPPTYP_VIRTDRV = 0x0080,
        FAPPTYP_PROTDLL = 0x0100,
        FAPPTYP_WINDOWSREAL = 0x0200,
        FAPPTYP_WINDOWSPROT = 0x0400,
        FAPPTYP_WINDOWSPROT31 = 0x1000,
        FAPPTYP_32BIT = 0x4000
    };

    enum
    {
        BEGIN_LIBPATH = 1,
        END_LIBPATH = 2,
        LIBPATHSTRICT = 3
    };

    typedef void *PCONTEXTRECORD;

    /* Undocumented DOSCALLS header query used by Java runtimes. */
    enum
    {
        QHINF_EXEINFO = 1,
        QHINF_READRSRCTBL,
        QHINF_READFILE,
        QHINF_LIBPATHLENGTH,
        QHINF_LIBPATH,
        QHINF_FIXENTRY,
        QHINF_STE,
        QHINF_MAPSEL
    };

    OS2EXPORT APIRET OS2API DosSetPriority(ULONG scope, ULONG priority_class, LONG delta,
                                           ULONG target);
    OS2EXPORT APIRET OS2API DosQueryHeaderInfo(HMODULE hmod, ULONG ulIndex, PVOID pvBuffer,
                                               ULONG cbBuffer, ULONG ulSubFunction);

    /* Undocumented DOSCALLS entry used by Java runtimes. */
    OS2EXPORT APIRET OS2API DosQuerySysState(ULONG func, ULONG arg1, ULONG pid, ULONG _res_,
                                             PVOID buf, ULONG bufsz);

    /* Undocumented DOSCALLS entry used by Java runtimes. */
    OS2EXPORT APIRET OS2API DosR3ExitAddr(void);

    /* Legacy USE16 DOSCALLS entries absent from the 4.5 Toolkit headers. */
    typedef int HSEM16, *PHSEM16;
    OS2EXPORT APIRET16 OS2API16 DosSemRequest(PHSEM16 sem, LONG ms);
    OS2EXPORT APIRET16 OS2API16 DosSemClear(PHSEM16 sem);
    OS2EXPORT APIRET16 OS2API16 DosSemWait(PHSEM16 sem, LONG ms);
    OS2EXPORT APIRET16 OS2API16 DosSemSet(PHSEM16 sem);
    /* DOSFSRSEM: USHORT cb, PID pid, TID tid, USHORT cUsage, USHORT client, ULONG sem. */
    typedef PVOID PDOSFSRSEM;
    OS2EXPORT APIRET16 OS2API16 Dos16FSRamSemRequest(PDOSFSRSEM sem, LONG timeout);
    OS2EXPORT APIRET16 OS2API16 Dos16FSRamSemClear(PDOSFSRSEM sem);

    /* USE16-only exports. */
    OS2EXPORT APIRET16 OS2API16 Dos16EnterCritSec(VOID);
    OS2EXPORT APIRET16 OS2API16 Dos16ExitCritSec(VOID);
    OS2EXPORT APIRET OS2API DosEnterCritSec(VOID);
    OS2EXPORT APIRET OS2API DosExitCritSec(VOID);
    OS2EXPORT APIRET16 OS2API16 Dos16SetPrty(USHORT scope, USHORT prtyclass, SHORT delta,
                                             USHORT pid);
    OS2EXPORT APIRET16 OS2API16 Dos16GetDateTime(PDATETIME pdt);
    OS2EXPORT APIRET16 OS2API16 Dos16Error(USHORT error);
    OS2EXPORT APIRET16 OS2API16 Dos16GetSeg(USHORT sel);
    OS2EXPORT APIRET16 OS2API16 Dos16LockSeg(USHORT sel);
    OS2EXPORT APIRET16 OS2API16 Dos16UnlockSeg(USHORT sel);
    OS2EXPORT APIRET16 OS2API16 Dos16HugeShift(PUSHORT pshift);
    OS2EXPORT APIRET16 OS2API16 Dos16HugeIncr(USHORT sel, PULONG psize);
    OS2EXPORT APIRET16 OS2API16 Dos16GetDBCSEv(USHORT buflen, PCOUNTRYCODE16 pcc, PCHAR buf);
    OS2EXPORT APIRET16 OS2API16 Dos16DevConfig(PVOID pdevinfo, USHORT item, USHORT reserved);
    OS2EXPORT APIRET16 OS2API16 Dos16DevIOCtl(PVOID pdata, PVOID pparms, USHORT fun,
                                              USHORT category, USHORT hdev);
    OS2EXPORT APIRET16 OS2API16 Dos16QFileInfo(USHORT hf, USHORT level, PVOID buf, USHORT cb);
    OS2EXPORT APIRET16 OS2API16 Dos16QPathInfo(PSZ path, USHORT level, PVOID buf, USHORT cb,
                                               ULONG reserved);
    OS2EXPORT APIRET16 OS2API16 Dos16BufReset(USHORT file);
    OS2EXPORT APIRET16 OS2API16 Dos16QFHandState(USHORT file, PUSHORT state);
    OS2EXPORT APIRET16 OS2API16 Dos16SetFHandState(USHORT file, USHORT state);
    OS2EXPORT APIRET16 OS2API16 Dos16SetFilePtrL(USHORT file, ULONG high, ULONG low, ULONG method,
                                                 PVOID position);
    OS2EXPORT APIRET16 OS2API16 Dos16SelectDisk(USHORT drive);
    OS2EXPORT APIRET16 OS2API16 Dos16SetFileInfo(USHORT hf, USHORT level, PVOID buf, USHORT cb);
    OS2EXPORT APIRET16 OS2API16 Dos16SetMaxFH(USHORT cfh);
    OS2EXPORT APIRET16 OS2API16 Dos16NewSize(USHORT hf, ULONG size);
    OS2EXPORT APIRET16 OS2API16 Dos16CreateCSAlias(USHORT selDS, PUSHORT pselCS);
    OS2EXPORT APIRET16 OS2API16 Dos16KillProcess(USHORT scope, USHORT pid);
    OS2EXPORT APIRET16 OS2API16 Dos16MakePipe(PUSHORT reader, PUSHORT writer, USHORT size);
    OS2EXPORT APIRET16 OS2API16 Dos16SetDateTime(PDATETIME datetime);
    OS2EXPORT APIRET16 OS2API16 Dos16GetShrSeg(PSZ name, PUSHORT selector);
    OS2EXPORT APIRET16 OS2API16 Dos16LoadModule(PCHAR failure, USHORT capacity, PSZ name,
                                                PUSHORT module);
    /* The encoded name accepts either a 16:16 string or a selector-zero ordinal. */
    OS2EXPORT APIRET16 OS2API16 Dos16GetProcAddr(USHORT module, ULONG name, PULONG address);
    OS2EXPORT APIRET16 OS2API16 Dos16FreeModule(USHORT module);
    OS2EXPORT APIRET16 OS2API16 Dos16GetModName(USHORT module, USHORT capacity, PCHAR name);
    OS2EXPORT APIRET16 OS2API16 Dos16QVerify(PUSHORT enabled);
    OS2EXPORT APIRET16 OS2API16 Dos16RmDir(PSZ path, ULONG reserved);
    OS2EXPORT APIRET16 OS2API16 Dos16SetVerify(USHORT enabled);
    OS2EXPORT APIRET16 OS2API16 Dos16DevIOCtl2(PVOID data, USHORT data_size, PVOID parameters,
                                               USHORT parameter_size, USHORT function,
                                               USHORT category, USHORT device);
    OS2EXPORT APIRET16 OS2API16 Dos16SizeSeg(USHORT selector, PULONG size);
    OS2EXPORT APIRET16 OS2API16 Dos16SearchPath(USHORT flags, PSZ path, PSZ name, PBYTE result,
                                                USHORT capacity);
    OS2EXPORT APIRET16 OS2API16 Dos16SetCp(USHORT codepage, USHORT reserved);
    OS2EXPORT APIRET16 OS2API16 Dos16QAppType(PSZ name, PUSHORT flags);
    OS2EXPORT APIRET16 OS2API16 Dos16QFSAttach(PSZ device, USHORT ordinal, USHORT level,
                                               PVOID buffer, PUSHORT size, ULONG reserved);
    OS2EXPORT APIRET16 OS2API16 Dos16ForceDelete(PSZ path, ULONG reserved);
    OS2EXPORT APIRET16 OS2API16 Dos16SetExtLIBPATH(PSZ path, ULONG flags);
    OS2EXPORT APIRET16 OS2API16 Dos16QueryExtLIBPATH(PCHAR path, ULONG flags);
    OS2EXPORT APIRET16 OS2API16 Dos16GetModHandle(PSZ name, PUSHORT module);
    OS2EXPORT APIRET16 OS2API16 Dos16CreateThread(PVOID pfnFun, PVOID pTid, PVOID pbStack);
    OS2EXPORT APIRET16 OS2API16 Dos16GetVersion(PUSHORT pver);
    OS2EXPORT APIRET16 OS2API16 Dos16QProcStatus(PVOID buffer, USHORT size);
    OS2EXPORT APIRET16 OS2API16 Dos16SMPMPresent(PUSHORT present);
    OS2EXPORT APIRET16 OS2API16 Dos16ScanEnv(PSZ name, PULONG value);
    OS2EXPORT APIRET16 OS2API16 Dos16SetProcCp(USHORT codepage, USHORT reserved);
    OS2EXPORT APIRET16 OS2API16 Dos16SMSetTitle(PSZ title);
    OS2EXPORT APIRET16 OS2API16 Dos16GetMachineMode(PBYTE pmode);
    OS2EXPORT APIRET16 OS2API16 Dos16GetHugeShift(PUSHORT pcount);
    OS2EXPORT VOID OS2API16 Dos16Exit(USHORT action, USHORT result);
    OS2EXPORT APIRET16 OS2API16 Dos16QHandType(USHORT handle, PUSHORT ptype, PUSHORT pflags);
    OS2EXPORT APIRET16 OS2API16 Dos16AllocSeg(USHORT size, PUSHORT psel, USHORT flags);
    OS2EXPORT APIRET16 OS2API16 Dos16ReallocSeg(USHORT size, USHORT sel);
    OS2EXPORT APIRET16 OS2API16 Dos16EditName(USHORT level, PSZ source, PSZ edit, PBYTE target,
                                              USHORT capacity);
    OS2EXPORT APIRET16 OS2API16 Dos16SubSet(USHORT selector, USHORT flags, USHORT size);
    OS2EXPORT APIRET16 OS2API16 Dos16SubAlloc(USHORT selector, PUSHORT offset, USHORT size);
    OS2EXPORT APIRET16 OS2API16 Dos16SubFree(USHORT selector, USHORT offset, USHORT size);
    OS2EXPORT APIRET16 OS2API16 Dos16FreeSeg(USHORT sel);

    typedef struct
    {
        USHORT pid;
        USHORT tid;
        USHORT pidParent;
    } PIDINFO16, *PPIDINFO16;

    OS2EXPORT APIRET16 OS2API16 Dos16GetPID(PPIDINFO16 ppidinfo);
    OS2EXPORT APIRET16 OS2API16 Dos16GetCp(USHORT cb, PUSHORT arCP, PUSHORT pcCP);
    OS2EXPORT APIRET16 OS2API16 Dos16GetEnv(PUSHORT psel, PUSHORT pcmdoffset);
    OS2EXPORT APIRET16 OS2API16 Dos16Write(USHORT h, PVOID buf, USHORT buflen, PUSHORT actual);
    OS2EXPORT APIRET16 OS2API16 Dos16SetVec(USHORT vecnum, PFN routine,
                                            PVOID prevaddress); /* prevaddress is a PFN * */
    OS2EXPORT APIRET16 OS2API16 Dos16SetSigHandler(
        PFN routine, PVOID prevaddress, PUSHORT prevaction, USHORT action,
        USHORT signumber); /* prevaddress is a PFNSIGHANDLER * routine is a PFNSIGHANDLER */
    OS2EXPORT APIRET16 OS2API16 Dos16GetInfoSeg(PSEL globalseg, PSEL localseg);
    OS2EXPORT APIRET16 OS2API16 Dos16QCurDisk(PUSHORT drivenum, PULONG drivemap);
    OS2EXPORT APIRET16 OS2API16 Dos16QCurDir(USHORT drivenum, PBYTE dirpath, PUSHORT dirpathlen);
    OS2EXPORT APIRET16 OS2API16 Dos16HoldSignal(USHORT action);
    OS2EXPORT APIRET16 OS2API16 Dos16QFsInfo(USHORT drivenum, USHORT fslevel, PBYTE infobuf,
                                             USHORT infobufsize);
    OS2EXPORT APIRET16 OS2API16 Dos16SetFileMode(PCHAR filepath, USHORT attr, ULONG reserved);
    OS2EXPORT APIRET16 OS2API16 Dos16QFileMode(PCHAR filepath, PUSHORT attr, ULONG res);
    OS2EXPORT APIRET16 OS2API16 Dos16Open(PSZ pszFileName, PUSHORT pHf, PUSHORT pusAction,
                                          ULONG cbFile, USHORT ulAttribute, USHORT fsOpenFlags,
                                          USHORT fsOpenMode, ULONG ulReserved);
    OS2EXPORT APIRET16 OS2API16 Dos16Open2(PSZ pszFileName, PUSHORT pHf, PUSHORT pusAction,
                                           ULONG cbFile, USHORT ulAttribute, USHORT fsOpenFlags,
                                           ULONG fsOpenMode, PVOID ea, ULONG reserved);
    OS2EXPORT APIRET16 OS2API16 Dos16OpenL(PSZ name, PUSHORT file, PUSHORT action, ULONG high,
                                           ULONG low, USHORT attributes, USHORT flags, ULONG mode,
                                           PVOID ea, ULONG reserved);
    OS2EXPORT APIRET16 OS2API16 Dos16FindFirst(PSZ pszFileSpec, PSHORT phdir, USHORT flAttribute,
                                               PVOID pfindbuf, USHORT cbBuf, PUSHORT pcFileNames,
                                               ULONG res);
    OS2EXPORT APIRET16 OS2API16 Dos16FindFirst2(PSZ pszFileSpec, PSHORT phdir, USHORT flAttribute,
                                                PVOID pfindbuf, USHORT cbBuf, PUSHORT pcFileNames,
                                                USHORT level, ULONG res);
    OS2EXPORT APIRET16 OS2API16 Dos16ChDir(PSZ pszDir, ULONG res);
    OS2EXPORT APIRET16 OS2API16 Dos16Close(USHORT hFile);
    OS2EXPORT APIRET16 OS2API16 Dos16FindNext(USHORT hDir, PVOID pfindbuf, USHORT cbfindbuf,
                                              PUSHORT pcFilenames);
    OS2EXPORT APIRET16 OS2API16 Dos16FindClose(USHORT hDir);
    OS2EXPORT APIRET16 OS2API16 Dos16Delete(PSZ pszFile, ULONG res);
    OS2EXPORT APIRET16 OS2API16 Dos16DupHandle(USHORT hFile, PUSHORT pHfile);
    OS2EXPORT APIRET16 OS2API16 Dos16Read(USHORT hFile, PVOID pBuffer, USHORT cbRead,
                                          PUSHORT pcbActual);
    OS2EXPORT APIRET16 OS2API16 Dos16ExecPgm(PCHAR pObjname, SHORT cbObjname, USHORT execFlag,
                                             PSZ pArg, PSZ pEnv, PRESULTCODES16 pRes, PSZ pName);
    OS2EXPORT APIRET16 OS2API16 Dos16CWait(USHORT action, USHORT option, PRESULTCODES16 pres,
                                           PUSHORT ppid, USHORT pid);
    OS2EXPORT APIRET16 OS2API16 Dos16ChgFilePtr(USHORT handle, LONG distance, USHORT whence,
                                                PULONG newoffset);
    OS2EXPORT APIRET16 OS2API16 Dos16Beep(USHORT freq, USHORT dur);
    OS2EXPORT APIRET16 OS2API16 Dos16MkDir(PSZ pszDirName, PEAOP2 peaop2);
    OS2EXPORT APIRET16 OS2API16 Dos16MkDir2(PSZ pszDirName, PEAOP2 peaop2);

    OS2EXPORT APIRET OS2API DosDupHandle(HFILE hFile, PHFILE pHfile);
    OS2EXPORT APIRET OS2API DosGetInfoBlocks(PTIB *pptib, PPIB *pppib);
    OS2EXPORT APIRET OS2API DosQuerySysInfo(ULONG iStart, ULONG iLast, PVOID pBuf, ULONG cbBuf);
    OS2EXPORT APIRET OS2API DosQueryModuleName(HMODULE hmod, ULONG cbName, PCHAR pch);
    OS2EXPORT APIRET OS2API DosScanEnv(PSZ pszName, PSZ *ppszValue);
    OS2EXPORT APIRET OS2API DosSearchPath(ULONG flag, PSZ pszPathOrName, PSZ pszFilename,
                                          PBYTE pBuf, ULONG cbBuf);
    OS2EXPORT APIRET OS2API DosWrite(HFILE hFile, PVOID pBuffer, ULONG cbWrite, PULONG pcbActual);
    OS2EXPORT VOID OS2API DosExit(ULONG action, ULONG result);
    OS2EXPORT APIRET OS2API DosExitList(ULONG ordercode, PFNEXITLIST pfn);

    typedef struct
    {
        ULONG hsemCur;
        ULONG ulUser;
    } SEMRECORD, *PSEMRECORD;

    OS2EXPORT APIRET OS2API DosCreateMuxWaitSem(PSZ name, PHMUX output, ULONG count,
                                                PSEMRECORD records, ULONG flags);
    OS2EXPORT APIRET OS2API DosOpenMuxWaitSem(PSZ name, PHMUX output);
    OS2EXPORT APIRET OS2API DosCloseMuxWaitSem(HMUX handle);
    OS2EXPORT APIRET OS2API DosWaitMuxWaitSem(HMUX handle, ULONG timeout, PULONG user);
    OS2EXPORT APIRET OS2API DosAddMuxWaitSem(HMUX handle, PSEMRECORD record);
    OS2EXPORT APIRET OS2API DosDeleteMuxWaitSem(HMUX handle, ULONG semaphore);
    OS2EXPORT APIRET OS2API DosQueryMuxWaitSem(HMUX handle, PULONG count, PSEMRECORD records,
                                               PULONG flags);
    OS2EXPORT APIRET OS2API DosCreateEventSem(PSZ pszName, PHEV phev, ULONG flAttr, BOOL32 fState);
    OS2EXPORT APIRET OS2API DosCreateMutexSem(PSZ pszName, PHMTX phmtx, ULONG flAttr,
                                              BOOL32 fState);
    OS2EXPORT APIRET OS2API DosOpenMutexSem(PSZ pszName, PHMTX phmtx);
    OS2EXPORT APIRET OS2API DosSetExceptionHandler(PEXCEPTIONREGISTRATIONRECORD pERegRec);
    OS2EXPORT APIRET OS2API DosUnsetExceptionHandler(PEXCEPTIONREGISTRATIONRECORD pERegRec);
    OS2EXPORT ULONG OS2API DosFlatToSel(VOID);
    OS2EXPORT APIRET OS2API DosSetSignalExceptionFocus(BOOL32 flag, PULONG pulTimes);
    OS2EXPORT APIRET OS2API DosSetRelMaxFH(PLONG pcbReqCount, PULONG pcbCurMaxFH);
    OS2EXPORT APIRET OS2API DosAllocMem(PPVOID ppb, ULONG cb, ULONG flag);
    OS2EXPORT APIRET OS2API DosGetNamedSharedMem(PPVOID ppb, PSZ pszName, ULONG flag);
    OS2EXPORT BOOL lx_dos_shared_address(ULONG address, ULONG size);
    OS2EXPORT APIRET DosGetSharedMemAddress(ULONG address, ULONG flags);
    OS2EXPORT APIRET OS2API DosAllocSharedMem(PPVOID ppb, PSZ pszName, ULONG cb, ULONG flag);
    OS2EXPORT APIRET OS2API DosGiveSharedMem(PVOID pb, PID pid, ULONG flag);
    OS2EXPORT APIRET OS2API DosSubUnsetMem(PVOID pbBase);
    OS2EXPORT APIRET OS2API DosSubSetMem(PVOID pbBase, ULONG flag, ULONG cb);
    OS2EXPORT APIRET OS2API DosSubAllocMem(PVOID pbBase, PPVOID ppb, ULONG cb);
    OS2EXPORT APIRET OS2API DosQueryHType(HFILE hFile, PULONG pType, PULONG pAttr);
    OS2EXPORT APIRET OS2API DosSetMem(PVOID pb, ULONG cb, ULONG flag);
    OS2EXPORT APIRET OS2API DosSuspendThread(TID id);
    OS2EXPORT APIRET OS2API DosResumeThread(TID id);
    OS2EXPORT APIRET OS2API DosGetDateTime(PDATETIME pdt);
    OS2EXPORT APIRET OS2API DosSetDateTime(PDATETIME pdt);
    OS2EXPORT APIRET OS2API DosOpen(PSZ pszFileName, PHFILE pHf, PULONG pulAction, ULONG cbFile,
                                    ULONG ulAttribute, ULONG fsOpenFlags, ULONG fsOpenMode,
                                    PEAOP2 peaop2);
    OS2EXPORT APIRET OS2API DosRequestMutexSem(HMTX hmtx, ULONG ulTimeout);
    OS2EXPORT APIRET OS2API DosReleaseMutexSem(HMTX hmtx);
    OS2EXPORT APIRET OS2API DosSetFilePtr(HFILE hFile, LONG ib, ULONG method, PULONG ibActual);
    OS2EXPORT APIRET OS2API DosRead(HFILE hFile, PVOID pBuffer, ULONG cbRead, PULONG pcbActual);
    OS2EXPORT APIRET OS2API DosClose(HFILE hFile);
    OS2EXPORT APIRET OS2API DosEnterMustComplete(PULONG pulNesting);
    OS2EXPORT APIRET OS2API DosExitMustComplete(PULONG pulNesting);
    OS2EXPORT APIRET OS2API DosQueryPathInfo(PSZ pszPathName, ULONG ulInfoLevel, PVOID pInfoBuf,
                                             ULONG cbInfoBuf);
    OS2EXPORT APIRET OS2API DosSetFileInfo(HFILE hf, ULONG ulInfoLevel, PVOID pInfoBuf,
                                           ULONG cbInfoBuf);
    OS2EXPORT APIRET OS2API DosSetDefaultDisk(ULONG disknum);
    OS2EXPORT APIRET OS2API DosQueryDBCSEnv(ULONG cb, PCOUNTRYCODE pcc, PCHAR pBuf);
    OS2EXPORT APIRET OS2API DosDeleteDir(PSZ pszDir);
    OS2EXPORT APIRET OS2API DosCopy(PSZ pszOld, PSZ pszNew, ULONG option);
    OS2EXPORT APIRET OS2API DosMove(PSZ pszOld, PSZ pszNew);
    OS2EXPORT APIRET16 OS2API16 Dos16Move(PSZ pszOld, PSZ pszNew, ULONG reserved);
    OS2EXPORT APIRET OS2API DosQueryMem(PVOID pb, PULONG pcb, PULONG pflag);
    OS2EXPORT APIRET OS2API DosError(ULONG error);
    OS2EXPORT APIRET OS2API DosQueryModFromEIP(PHMODULE phmod, PULONG pulObject, ULONG cbModName,
                                               PCHAR pszModName, PULONG pulOffset, ULONG ulEIP);
    OS2EXPORT APIRET OS2API DosAcknowledgeSignalException(ULONG ulSignalNum);
    OS2EXPORT APIRET OS2API DosRaiseException(PEXCEPTIONREPORTRECORD pexcept);
    OS2EXPORT APIRET OS2API DosUnwindException(PEXCEPTIONREGISTRATIONRECORD phandler,
                                               PVOID pTargetIP, PEXCEPTIONREPORTRECORD pERepRec);
    OS2EXPORT APIRET OS2API DosQueryFileInfo(HFILE hf, ULONG ulInfoLevel, PVOID pInfo,
                                             ULONG cbInfoBuf);
    OS2EXPORT APIRET OS2API DosCreateThread(PTID ptid, PFNTHREAD pfn, ULONG param, ULONG flag,
                                            ULONG cbStack);
    OS2EXPORT APIRET OS2API DosExecPgm(PCHAR pObjname, LONG cbObjname, ULONG execFlag, PSZ pArg,
                                       PSZ pEnv, PRESULTCODES pRes, PSZ pName);
    OS2EXPORT APIRET OS2API DosResetEventSem(HEV hev, PULONG pulPostCt);
    OS2EXPORT APIRET OS2API DosPostEventSem(HEV hev);
    OS2EXPORT APIRET OS2API DosCloseEventSem(HEV hev);
    OS2EXPORT APIRET OS2API DosWaitEventSem(HEV hev, ULONG ulTimeout);
    OS2EXPORT APIRET OS2API DosKillProcess(ULONG action, PID process);
    OS2EXPORT APIRET OS2API DosOpenEventSem(PSZ pszName, PHEV phev);
    OS2EXPORT APIRET OS2API DosWaitNPipe(PSZ pszName, ULONG ulTimeOut);
    OS2EXPORT APIRET OS2API DosQueryEventSem(HEV hev, PULONG pulPostCt);
    OS2EXPORT APIRET OS2API DosFreeMem(PVOID pb);
    OS2EXPORT APIRET OS2API DosWaitChild(ULONG action, ULONG option, PRESULTCODES pres, PPID ppid,
                                         PID pid);
    OS2EXPORT APIRET OS2API DosKillThread(TID id);
    OS2EXPORT APIRET OS2API DosWaitThread(PTID ptid, ULONG option);
    OS2EXPORT APIRET OS2API DosSleep(ULONG msec);
    OS2EXPORT APIRET OS2API DosSubFreeMem(PVOID pbBase, PVOID pb, ULONG cb);
    OS2EXPORT APIRET OS2API DosDelete(PSZ pszFile);
    OS2EXPORT APIRET OS2API DosForceDelete(PSZ pszFile);
    OS2EXPORT APIRET OS2API DosQueryCurrentDir(ULONG disknum, PBYTE pBuf, PULONG pcbBuf);
    OS2EXPORT APIRET OS2API DosSetPathInfo(PSZ pszPathName, ULONG ulInfoLevel, PVOID pInfoBuf,
                                           ULONG cbInfoBuf, ULONG flOptions);
    OS2EXPORT APIRET OS2API DosQueryModuleHandle(PSZ pszModname, PHMODULE phmod);
    OS2EXPORT APIRET OS2API DosGetResource(HMODULE module, ULONG type, ULONG name, PPVOID output);
    OS2EXPORT APIRET OS2API DosFreeResource(PVOID resource);
    OS2EXPORT APIRET OS2API DosQueryResourceSize(HMODULE module, ULONG type, ULONG name,
                                                 PULONG size);
    OS2EXPORT APIRET OS2API Dos32QueryResourceSize(HMODULE module, ULONG type, ULONG name,
                                                   PULONG size);
    OS2EXPORT APIRET OS2API DosQueryProcAddr(HMODULE hmod, ULONG ordinal, PSZ pszName, PFN *ppfn);
    OS2EXPORT APIRET OS2API DosQueryCp(ULONG cb, PULONG arCP, PULONG pcCP);
    OS2EXPORT APIRET OS2API DosOpenL(PSZ pszFileName, PHFILE pHf, PULONG pulAction, LONGLONG cbFile,
                                     ULONG ulAttribute, ULONG fsOpenFlags, ULONG fsOpenMode,
                                     PEAOP2 peaop2);
    OS2EXPORT APIRET OS2API DosFindFirst(PSZ pszFileSpec, PHDIR phdir, ULONG flAttribute,
                                         PVOID pfindbuf, ULONG cbBuf, PULONG pcFileNames,
                                         ULONG ulInfoLevel);
    OS2EXPORT APIRET OS2API DosFindNext(HDIR hDir, PVOID pfindbuf, ULONG cbfindbuf,
                                        PULONG pcFilenames);
    OS2EXPORT APIRET OS2API DosFindClose(HDIR hDir);
    OS2EXPORT APIRET OS2API DosQueryCurrentDisk(PULONG pdisknum, PULONG plogical);
    OS2EXPORT APIRET OS2API DosDevConfig(PVOID pdevinfo, ULONG item);
    OS2EXPORT APIRET OS2API DosLoadModule(PSZ pszName, ULONG cbName, PSZ pszModname,
                                          PHMODULE phmod);
    OS2EXPORT APIRET OS2API DosFreeModule(HMODULE hmod);
    OS2EXPORT APIRET OS2API DosResetBuffer(HFILE hFile);
    OS2EXPORT APIRET OS2API DosQueryProcType(HMODULE module, ULONG ordinal, PSZ name, PULONG type);
    OS2EXPORT APIRET OS2API DosEditName(ULONG level, PSZ source, PSZ edit, PBYTE target,
                                        ULONG capacity);
    OS2EXPORT APIRET OS2API DosTmrQueryFreq(PULONG frequency);
    OS2EXPORT APIRET OS2API DosTmrQueryTime(PVOID ticks);
    OS2EXPORT APIRET OS2API DosEnumAttribute(ULONG reference_type, PVOID reference, ULONG first,
                                             PVOID buffer, ULONG capacity, PULONG count,
                                             ULONG level);
    OS2EXPORT APIRET OS2API DosShutdown(ULONG reserved);
    OS2EXPORT APIRET OS2API DosGetSharedMem(PVOID address, ULONG flags);
    OS2EXPORT APIRET OS2API DosQueryAppType(PSZ pszName, PULONG pFlags);
    OS2EXPORT APIRET OS2API DosAllocThreadLocalMemory(ULONG cb, PULONG *p);
    OS2EXPORT APIRET OS2API DosFreeThreadLocalMemory(ULONG *p);
    OS2EXPORT APIRET OS2API DosQueryFHState(HFILE hFile, PULONG pMode);
    OS2EXPORT APIRET OS2API DosSetExtLIBPATH(PSZ path, ULONG flags);
    OS2EXPORT APIRET OS2API DosQueryExtLIBPATH(PSZ pszExtLIBPATH, ULONG flags);
    OS2EXPORT APIRET OS2API DosSetMaxFH(ULONG cFH);
    OS2EXPORT APIRET OS2API DosQueryThreadContext(TID tid, ULONG level, PCONTEXTRECORD pcxt);
    OS2EXPORT ULONG OS2API DosSelToFlat(VOID);
    OS2EXPORT APIRET OS2API DosCloseMutexSem(HMTX hmtx);
    OS2EXPORT APIRET OS2API DosSetProcessCp(ULONG cp);
    OS2EXPORT APIRET OS2API DosDevIOCtl(HFILE device, ULONG category, ULONG function,
                                        PVOID parameters, ULONG parameter_max,
                                        PULONG parameter_size, PVOID data, ULONG data_max,
                                        PULONG data_size);
    OS2EXPORT APIRET OS2API DosQueryFSInfo(ULONG drive, ULONG level, PVOID data, ULONG capacity);
    OS2EXPORT APIRET OS2API DosSetFSInfo(ULONG drive, ULONG level, PVOID data, ULONG size);
    OS2EXPORT APIRET OS2API DosQueryFSAttach(PSZ pszDeviceName, ULONG ulOrdinal,
                                             ULONG ulFSAInfoLevel, PFSQBUFFER2 pfsqb,
                                             PULONG pcbBuffLength);
    OS2EXPORT APIRET OS2API DosSetFileSize(HFILE hFile, ULONG cbSize);
    OS2EXPORT APIRET OS2API DosSetFileLocks(HFILE hFile, PFILELOCK unlock, PFILELOCK lock,
                                            ULONG timeout, ULONG flags);
    OS2EXPORT APIRET OS2API DosSetCurrentDir(PSZ pszName);
    OS2EXPORT APIRET OS2API DosBeep(ULONG freq, ULONG dur);
    OS2EXPORT APIRET OS2API DosCreateDir(PSZ pszDirName, PEAOP2 peaop2);
    typedef HANDLE HSEM;
    typedef HANDLE HTIMER, *PHTIMER;
    OS2EXPORT APIRET OS2API DosAsyncTimer(ULONG msec, HSEM hsem, PHTIMER phtimer);
    OS2EXPORT APIRET OS2API DosStartTimer(ULONG msec, HSEM hsem, PHTIMER phtimer);
    OS2EXPORT APIRET OS2API DosStopTimer(HTIMER htimer);
    OS2EXPORT APIRET OS2API DosQueryMutexSem(HMTX hmtx, PPID ppid, PTID ptid, PULONG pulCount);
    /* Register-driven entry; see os2libs/doscalls/unithunk.c. */
    OS2EXPORT APIRET OS2API THK_UNITHUNK(VOID);

#ifdef __cplusplus
}
#endif

#endif
