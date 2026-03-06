/**
 * Minimal COM/Win32 type compat header for macOS.
 * Provides enough for DxbcConverter.h to compile.
 */
#pragma once
#ifndef MACOS_COM_COMPAT_H
#define MACOS_COM_COMPAT_H

#if !defined(_WIN32)

#include <cstdint>
#include <cstdlib>
#include <cstring>

typedef unsigned long  ULONG;
typedef long           LONG;
typedef int            BOOL;
typedef unsigned int   UINT;
typedef unsigned int   UINT32;
typedef unsigned short UINT16;
typedef unsigned char  UINT8;
typedef unsigned char  BYTE;
typedef void*          LPVOID;
typedef const void*    LPCVOID;
typedef wchar_t*       LPWSTR;
typedef const wchar_t* LPCWSTR;
typedef long           HRESULT;

#define S_OK    ((HRESULT)0L)
#define S_FALSE ((HRESULT)1L)
#define FAILED(hr) (((HRESULT)(hr)) < 0)
#define SUCCEEDED(hr) (((HRESULT)(hr)) >= 0)

#define __stdcall
#define STDMETHODCALLTYPE
#define STDAPICALLTYPE
#define WINAPI
#define __declspec(x)
#define _In_
#define _In_opt_z_
#define _In_reads_bytes_(x)
#define _Out_
#define _Outptr_result_bytebuffer_maybenull_(x)
#define _Outptr_result_maybenull_z_
#define DECLARE_CROSS_PLATFORM_UUIDOF(T)
#define DEFINE_CROSS_PLATFORM_UUIDOF(T)

struct GUID {
    uint32_t Data1;
    uint16_t Data2;
    uint16_t Data3;
    uint8_t  Data4[8];
};
typedef GUID CLSID;
typedef GUID IID;
typedef const IID& REFIID;
typedef const CLSID& REFCLSID;

#define __uuidof(T) T::uuidof()

// Minimal IUnknown
struct IUnknown {
    static const IID& uuidof() {
        static const IID iid = {0, 0, 0, {0,0,0,0,0,0,0,0}};
        return iid;
    }
    virtual HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObj) = 0;
    virtual ULONG   STDMETHODCALLTYPE AddRef() = 0;
    virtual ULONG   STDMETHODCALLTYPE Release() = 0;
    virtual ~IUnknown() = default;
};

struct IMalloc : IUnknown {};

inline void CoTaskMemFree(void* p) { free(p); }
inline void* CoTaskMemAlloc(size_t size) { return malloc(size); }

#endif  // !_WIN32
#endif  // MACOS_COM_COMPAT_H
