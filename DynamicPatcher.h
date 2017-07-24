﻿// created by i-saint
// distributed under Creative Commons Attribution (CC BY) license.
// https://github.com/i-saint/DynamicPatcher

#ifndef DynamicPatcher_h
#define DynamicPatcher_h

#ifndef dpDisable

#if _MSC_VER>=1600 // dpPatchByFile() require C++11
#   define dpWithStdFunction
#endif // _MSC_VER>=1600

#ifdef _M_X64
#   define dpLibArch "64"
#else // _M_X64
#   define dpLibArch "32"
#endif //_M_X64

#if   _MSC_VER==1500
#   define dpVCVersion "vc90"
#   define dpVCYear    2008
#elif _MSC_VER==1600
#   define dpVCVersion "vc100"
#   define dpVCYear    2010
#elif _MSC_VER==1700
#   define dpVCVersion "vc110"
#   define dpVCYear    2012
#elif _MSC_VER==1800
#   define dpVCVersion "vc120"
#   define dpVCYear    2013
#elif _MSC_VER==1900
#   define dpVCVersion "vc140"
#   define dpVCYear    2015
#elif _MSC_VER==1910
#   define dpVCVersion "vc141"
#   define dpVCYear    2017
#endif


#if defined(dpDLLImpl)
#   define dpAPI dpDLLExport
#elif !defined(dpNoLib) && !defined(dpAlcantarea)
#   define dpAPI dpDLLImport
#   pragma comment(lib,"DynamicPatcher" dpLibArch ".lib")
#else
#   define dpAPI
#endif // dpDLL_Impl
#ifdef dpWithStdFunction
#   include <functional>
#endif // dpWithStdFunction

#define dpDLLExport     __declspec(dllexport)
#define dpDLLImport     __declspec(dllimport)
#define dpCLinkage      extern "C"

#define dpPatch         dpDLLExport
#define dpNoInline      __declspec(noinline)
#define dpScope(...)    __VA_ARGS__
#define dpOnLoad(...)   dpCLinkage static void dpOnLoadHandler()  { __VA_ARGS__ } __declspec(selectany) void *_dpOnLoadHandler  =dpOnLoadHandler;
#define dpOnUnload(...) dpCLinkage static void dpOnUnloadHandler(){ __VA_ARGS__ } __declspec(selectany) void *_dpOnUnloadHandler=dpOnUnloadHandler;

enum dpSymbolFlags {
    dpE_Code    = 0x1,  // code
    dpE_IData   = 0x2,  // initialized data
    dpE_UData   = 0x4,  // uninitialized data
    dpE_Read    = 0x8,  // readable
    dpE_Write   = 0x10, // writable
    dpE_Execute = 0x20, // executable
    dpE_Shared  = 0x40, // shared
    dpE_Export  = 0x80, // dllexport
    dpE_Handler = 0x100, // dpOnLoad, dpOnUnload, etc
};
#define dpIsFunction(flag) ((flag&dpE_Code)!=0)
#define dpIsExportFunction(flag) ((flag&(dpE_Code|dpE_Export|dpE_Handler))==(dpE_Code|dpE_Export))

struct dpSymbolS
{
    const char *name;
    void *address;
    int flags;
};


enum dpLogLevel {
    dpE_LogError   = 0x1,
    dpE_LogWarning = 0x2,
    dpE_LogInfo    = 0x4,
    dpE_LogDetail  = 0x8,

    dpE_LogAll      = dpE_LogError|dpE_LogWarning|dpE_LogInfo|dpE_LogDetail,
    dpE_LogSimple   = dpE_LogError|dpE_LogWarning|dpE_LogInfo,
    dpE_LogNone     = 0,
};
enum dpSystemFlags {
    dpE_SysPatchExports          = 0x1, // patch exported (dllexport==dpPatch) functions automatically when modules are loaded
    dpE_SysDelayedLink           = 0x2,
    dpE_SysLoadConfig            = 0x4,
    dpE_SysOpenConsole           = 0x8,
    dpE_SysRunCommunicator       = 0x10,
    dpE_SysCommunicatorAutoFlush = 0x20,

    dpE_SysDefault = dpE_SysPatchExports | dpE_SysDelayedLink | dpE_SysLoadConfig,
};

#define dpDefaultCommunicatorPort 14841


struct dpConfig
{
    int log_flags; // combination of dpLogLevel
    int sys_flags; // combination of dpSystemFlags
    int vc_ver; // VisualC++ version to use to build. 2008/2010/2012
    const char *configfile;
    unsigned long long starttime;
    unsigned short communicator_port;

    dpConfig(int log=dpE_LogSimple, int f=dpE_SysDefault, const char *config=NULL, unsigned short com_port=dpDefaultCommunicatorPort)
        : log_flags(log), sys_flags(f), configfile(config), vc_ver(0), starttime(), communicator_port(com_port)
    {
        vc_ver = dpVCYear;
    }
};

class dpContext;

dpAPI bool   dpInitialize(const dpConfig &conf=dpConfig());
dpAPI bool   dpFinalize();

dpAPI dpContext* dpGetDefaultContext();
dpAPI dpContext* dpCreateContext();
dpAPI void       dpDeleteContext(dpContext *ctx);
dpAPI void       dpSetCurrentContext(dpContext *ctx); // current context is thread local
dpAPI dpContext* dpGetCurrentContext(); // default is dpGetDefaultContext()

dpAPI size_t dpLoad(const char *path); // path to .obj .lib .dll .exe. accepts wildcard (ex: x64/Debug/*.obj)
dpAPI bool   dpLoadObj(const char *path); // load as .obj regardless file extension
dpAPI bool   dpLoadLib(const char *path); // load as .lib regardless file extension
dpAPI bool   dpLoadDll(const char *path); // load as .dll regardless file extension
dpAPI bool   dpUnload(const char *path);
dpAPI bool   dpLink(); // must be called after dpLoad*()s & dpUnload()s. onload handler is called in this.
dpAPI size_t dpLoadMapFiles();  // load symbol info from .map files. this function will reduce link time for .obj files drastically.
                                // .map files should be placed on same directory of .exe or .dll with same name.
                                // ex) c:/foo/bar.exe -> c:/foo/bar.map
                                // .map files will be generated if linker option /map is specified.

dpAPI size_t dpPatchByFile(const char *filename, const char *filter_regex); // ex: dpPatchByFile("MyClass.obj", "MyClass::.*")
#ifdef dpWithStdFunction
dpAPI size_t dpPatchByFile(const char *filename, const std::function<bool (const dpSymbolS&)> &condition);
#endif // dpWithStdFunction
dpAPI bool   dpPatchNameToName(const char *target_name, const char *hook_name);
dpAPI bool   dpPatchAddressToName(const char *target_name, void *hook_addr);
dpAPI bool   dpPatchAddressToAddress(void *target, void *hook_addr);
dpAPI bool   dpPatchByAddress(void *hook_addr); // patches the host symbol that have same name of hook
dpAPI bool   dpUnpatchByAddress(void *target_or_hook_addr);
dpAPI void   dpUnpatchAll();
dpAPI void*  dpGetUnpatched(void *target_or_hook_addr);
dpAPI void   dpAddForceHostSymbolPattern(const char *pattern);

dpAPI void   dpAddModulePath(const char *path); // accepts wildcard. affects auto build and dpReload()
dpAPI void   dpAddSourcePath(const char *path); // 
dpAPI void   dpAddPreloadPath(const char *path); // 
dpAPI void   dpAddMSBuildCommand(const char *msbuild_option); // add msbuild command that will be called by auto build thread
dpAPI void   dpAddCLBuildCommand(const char *cl_option); // add cl command that will be called by auto build thread
dpAPI void   dpAddBuildCommand(const char *any_command); // add arbitrary command that will be called by auto build thread
dpAPI bool   dpStartAutoBuild();
dpAPI bool   dpStopAutoBuild();
dpAPI bool   dpStartPreload();
dpAPI bool   dpStopPreload();
dpAPI void   dpUpdate(); // reloads and links modified modules.

dpAPI void          dpPrint(const char* fmt, ...);
dpAPI bool          dpDemangleSignatured(const char *mangled, char *demangled, size_t buflen);
dpAPI bool          dpDemangleNameOnly(const char *mangled, char *demangled, size_t buflen);
dpAPI const char*   dpGetVCVarsPath();

#else  // dpDisable

#define dpPatch 
#define dpNoInline 
#define dpScope(...) 
#define dpOnLoad(...) 
#define dpOnUnload(...) 

#define dpInitialize(...) 
#define dpFinalize(...) 

#define dpGetDefaultContext(...) 
#define dpCreateContext(...) 
#define dpDeleteContext(...) 
#define dpSetCurrentContext(...) 
#define dpGetCurrentContext(...) 

#define dpLoad(...) 
#define dpLoadObj(...) 
#define dpLoadLib(...) 
#define dpLoadDll(...) 
#define dpUnload(...) 
#define dpLink(...) 
#define dpLoadMapFiles(...) 
#define dpPatchByFile(...) 
#define dpPatchNameToName(...) 
#define dpPatchAddressToName(...) 
#define dpPatchAddressToAddress(...) 
#define dpPatchByAddress(...) 
#define dpUnpatchByAddress(...)
#define dpUnpatchAll(...)
#define dpGetUnpatched(...) 
#define dpAddForceHostSymbolPattern(...) 

#define dpAddModulePath(...) 
#define dpAddSourcePath(...) 
#define dpAddPreloadPath(...) 
#define dpAddMSBuildCommand(...) 
#define dpAddCLBuildCommand(...) 
#define dpAddBuildCommand(...) 
#define dpStartAutoBuild(...) 
#define dpStopAutoBuild(...) 
#define dpStartPreload(...) 
#define dpStopPreload(...) 
#define dpUpdate(...) 

#define dpPrint(...) 
#define dpDemangleSignatured(...) 
#define dpDemangleNameOnly(...) 
#define dpGetVCVarsPath(...) 

#endif // dpDisable

#endif // DynamicPatcher_h
