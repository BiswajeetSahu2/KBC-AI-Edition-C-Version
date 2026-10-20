#ifndef KBC_WINDOWS_COMPAT_H
#define KBC_WINDOWS_COMPAT_H

#ifdef _WIN32
#include <windows.h>

VOID WINAPI InitializeSRWLock(PSRWLOCK lock);
VOID WINAPI AcquireSRWLockExclusive(PSRWLOCK lock);
BOOLEAN WINAPI TryAcquireSRWLockExclusive(PSRWLOCK lock);
VOID WINAPI ReleaseSRWLockExclusive(PSRWLOCK lock);
ULONGLONG WINAPI GetTickCount64(VOID);
#endif

#endif