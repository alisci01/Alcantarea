﻿// created by i-saint
// distributed under Creative Commons Attribution (CC BY) license.
// https://github.com/i-saint/DynamicPatcher

#include "dpInternal.h"
#include "dpObjFile.h"

#ifdef dpWithObjFile

static inline const char* dpGetSymbolName(PSTR pStringTable, PIMAGE_SYMBOL pSym)
{
    return pSym->N.Name.Short!=0 ? (const char*)&pSym->N.ShortName : (const char*)(pStringTable + pSym->N.Name.Long);
}

struct TIMAGE_SYMBOL
{
    const char *Name;
    DWORD   Value;
    LONG    SectionNumber;
    WORD    Type;
    BYTE    StorageClass;
    BYTE    NumberOfAuxSymbols;

    TIMAGE_SYMBOL(void *data, PSTR pStringTable, bool is_bigobj)
    {
        if(is_bigobj) {
            PIMAGE_SYMBOL_EX pSym = (PIMAGE_SYMBOL_EX)data;
            Name = dpGetSymbolName(pStringTable, (PIMAGE_SYMBOL)pSym);
            Value = pSym->Value;
            SectionNumber = pSym->SectionNumber;
            Type = pSym->Type;
            StorageClass = pSym->StorageClass;
            NumberOfAuxSymbols = pSym->NumberOfAuxSymbols;
        }
        else {
            PIMAGE_SYMBOL pSym = (PIMAGE_SYMBOL)data;
            Name = dpGetSymbolName(pStringTable, pSym);
            Value = pSym->Value;
            SectionNumber = pSym->SectionNumber;
            Type = pSym->Type;
            StorageClass = pSym->StorageClass;
            NumberOfAuxSymbols = pSym->NumberOfAuxSymbols;
        }
    }
};




dpObjFile::dpObjFile(dpContext *ctx)
    : dpBinary(ctx)
    , m_data(nullptr), m_size(0)
    , m_aligned_data(nullptr), m_aligned_datasize(0)
    , m_path(), m_mtime(0)
    , m_symbols()

    , m_IsBigobj(false)
    , m_Arch(0)
    , m_SymSize(0)
    , m_pSectionHeader(nullptr)
    , m_pSymbolTable(nullptr)
    , m_SectionCount(0)
    , m_SymbolCount(0)
    , m_StringTable(nullptr)
{
}

dpObjFile::~dpObjFile()
{
    unload();
}

void dpObjFile::unload()
{
    dpGetPatcher()->unpatchByBinary(this);
    eachSymbols([&](dpSymbol *sym){ dpGetLoader()->deleteSymbol(sym); });
    if(m_data!=NULL) {
        dpDeallocate(m_data);
        m_data = NULL;
        m_size = 0;
    }
    if(m_aligned_data!=NULL) {
        dpDeallocate(m_aligned_data);
        m_aligned_data = NULL;
        m_aligned_datasize = 0;
    }
    m_path.clear();
    m_symbols.clear();
}


bool dpObjFile::loadFile(const char *path)
{
    dpTime mtime = dpGetMTime(path);
    if(m_symbols.getNumSymbols()>0 && mtime<=m_mtime) { return true; }

    void *data;
    size_t size;
    if(!dpMapFile(path, data, size, dpAllocateModule)) {
        dpPrintError("file not found %s\n", path);
        return false;
    }
    return loadMemory(path, data, size, mtime);
}

bool dpObjFile::loadMemory(const char *path, void *data, size_t size, dpTime mtime)
{
    if(m_symbols.getNumSymbols()>0 && mtime<=m_mtime) { return true; }
    m_path = path; dpSanitizePath(m_path);
    m_data = data;
    m_size = size;
    m_mtime = mtime;

    size_t ImageBase = (size_t)(m_data);
    {
        PIMAGE_DOS_HEADER pDosHeader = (PIMAGE_DOS_HEADER)ImageBase;
        ANON_OBJECT_HEADER_BIGOBJ *pBigHeader = (ANON_OBJECT_HEADER_BIGOBJ*)ImageBase;
        if( (pDosHeader->e_magic==IMAGE_FILE_MACHINE_I386 || pDosHeader->e_magic==IMAGE_FILE_MACHINE_AMD64) && pDosHeader->e_sp==0 )
        {
            m_IsBigobj = false;
            m_Arch = pDosHeader->e_magic;
            PIMAGE_FILE_HEADER pImageHeader = (PIMAGE_FILE_HEADER)ImageBase;
            m_pSectionHeader = (PIMAGE_SECTION_HEADER)(ImageBase + sizeof(IMAGE_FILE_HEADER) + pImageHeader->SizeOfOptionalHeader);
            m_pSymbolTable = (char*)(ImageBase + pImageHeader->PointerToSymbolTable);
            m_SectionCount = pImageHeader->NumberOfSections;
            m_SymbolCount = pImageHeader->NumberOfSymbols;
        }
        else if(pBigHeader->Sig1==IMAGE_FILE_MACHINE_UNKNOWN && pBigHeader->Sig2==0xFFFF && pBigHeader->Version>=2 &&
            (pBigHeader->Machine==IMAGE_FILE_MACHINE_I386 || pBigHeader->Machine==IMAGE_FILE_MACHINE_AMD64) )
        {
            m_IsBigobj = true;
            m_Arch = pBigHeader->Machine;
            m_pSectionHeader = (PIMAGE_SECTION_HEADER)(ImageBase + sizeof(ANON_OBJECT_HEADER_BIGOBJ) + pBigHeader->SizeOfData);
            m_pSymbolTable = (char*)(ImageBase + pBigHeader->PointerToSymbolTable);
            m_SectionCount = pBigHeader->NumberOfSections;
            m_SymbolCount = pBigHeader->NumberOfSymbols;
        }
        else {
            dpPrintError("%s unknown file format. it might be compiled with /GL option.\n"
                , m_path.c_str());
            return false;
        }
#ifdef _M_X64
        if(m_Arch!=IMAGE_FILE_MACHINE_AMD64) {
            dpPrintError("%s is not for x64.\n" , m_path.c_str());
            return false;
        }
#else // _M_X64
        if(m_Arch!=IMAGE_FILE_MACHINE_I386) {
            dpPrintError("%s is not for x86.\n" , m_path.c_str());
            return false;
        }
#endif // _M_X64
        m_SymSize = m_IsBigobj ? sizeof(IMAGE_SYMBOL_EX) : sizeof(IMAGE_SYMBOL);
        m_StringTable = (PSTR)(m_pSymbolTable + m_SymSize*m_SymbolCount);
    }


    m_linkdata.resize(m_SectionCount);

    // アラインが必要な section をアラインしつつ新しい領域に移す
	// Move the section that needs to be aligned to a new region while aligning it
    m_aligned_data = nullptr;
    m_aligned_datasize = 0xffffffff;
    for(size_t ti=0; ti<2; ++ti) {
        // ti==0 で必要な容量を調べ、ti==1 で実際のメモリ確保と再配置を行う
		// check the required capacity with ti == 0 and reserve and relocate the actual memory at ti == 1
        dpSectionAllocator salloc(m_aligned_data, m_aligned_datasize);

        for(size_t si=0; si<m_SectionCount; ++si) {
            IMAGE_SECTION_HEADER &sect = m_pSectionHeader[si];
            // IMAGE_SECTION_HEADER::Characteristics にアライン情報が詰まっている
			// IMAGE_SECTION_HEADER::Characteristics is packed with memory alignment information
            DWORD align = 1 << (((sect.Characteristics & 0x00f00000) >> 20) - 1);
            if(align==1) {
                // do nothing
                continue;
            }
            else {
                if(void *rd = salloc.allocate(sect.SizeOfRawData, align)) {
                    if(sect.PointerToRawData != 0) {
                        memcpy(rd, (void*)(ImageBase + sect.PointerToRawData), sect.SizeOfRawData);
                    }
                    sect.PointerToRawData = (DWORD)((size_t)rd - ImageBase);
                }
            }
        }

        if(ti==0) {
            m_aligned_datasize = salloc.getUsed();
            m_aligned_data = dpAllocateForward(m_aligned_datasize, m_data);
        }
    }

    // symbol 収集処理
	// symbol collection process
    for( size_t i=0; i<m_SymbolCount; ++i ) {
        TIMAGE_SYMBOL sym(m_pSymbolTable + m_SymSize*i, m_StringTable, m_IsBigobj);
        if(sym.SectionNumber>0) {
            IMAGE_SECTION_HEADER &sect = m_pSectionHeader[sym.SectionNumber-1];
            m_relocdata.resize(m_relocdata.size()+sect.NumberOfRelocations);
            void *data = (void*)(ImageBase + (int)sect.PointerToRawData + sym.Value);
            if(sym.SectionNumber==IMAGE_SYM_UNDEFINED) { goto NextSection; }
            const char *name = sym.Name;
            if(name[0]!='.' && name[0]!='$') {
                DWORD flags = 0;
                if((sect.Characteristics&IMAGE_SCN_CNT_CODE))               { flags|=dpE_Code; }
                if((sect.Characteristics&IMAGE_SCN_CNT_INITIALIZED_DATA))   { flags|=dpE_IData; }
                if((sect.Characteristics&IMAGE_SCN_CNT_UNINITIALIZED_DATA)) { flags|=dpE_UData; }
                if((sect.Characteristics&IMAGE_SCN_MEM_READ))    { flags|=dpE_Read; }
                if((sect.Characteristics&IMAGE_SCN_MEM_WRITE))   { flags|=dpE_Write; }
                if((sect.Characteristics&IMAGE_SCN_MEM_EXECUTE)) { flags|=dpE_Execute; }
                if((sect.Characteristics&IMAGE_SCN_MEM_SHARED))  { flags|=dpE_Shared; }
                m_symbols.addSymbol(dpGetLoader()->newSymbol(name, data, flags, sym.SectionNumber-1, this));
            }
        }
    NextSection:
        i += sym.NumberOfAuxSymbols;
    }
    m_symbols.sort();

    for(size_t si=0; si<m_SectionCount; ++si) {
        IMAGE_SECTION_HEADER &sect = m_pSectionHeader[si];
        // .drectve section には linker directive が入っており、dllexport 付き symbol のリストがここに含まれる
		// .drectve section contains a linker directive and a list of symbols with dllexport is included here
        if(strncmp((char*)sect.Name, ".drectve", 8)==0) {
            char *data = (char*)(ImageBase + sect.PointerToRawData);
            data[sect.SizeOfRawData] = '\0';
            std::regex reg("/EXPORT:([^ ,]+)");
            std::cmatch m;
            size_t pos = 0;
            for(;;) {
                if(std::regex_search(data+pos, m, reg)) {
                    char *name = data+pos+m.position(1);
                    name[m.length(1)] = '\0';
                    if(dpSymbol *s = m_symbols.findSymbolByName(name)) {
                        s->flags |= dpE_Export;
                    }
                    pos += m.position()+m.length()+1;
                    if(pos>=sect.SizeOfRawData) { break; }
                }
                else {
                    break;
                }
            }
        }
    }
    if(dpSymbol *s=getSymbolTable().findSymbolByName(g_dpSymName_OnLoad))   { s->flags |= dpE_Handler; }
    if(dpSymbol *s=getSymbolTable().findSymbolByName(g_dpSymName_OnUnload)) { s->flags |= dpE_Handler; }
    return true;
}

// 外部シンボルのリンケージ解決
bool dpObjFile::link()
{
    size_t num_sections = m_linkdata.size();
    for(size_t si=0; si<num_sections; ++si) {
        m_linkdata[si].flags |= dpE_NeedsLink;
    }
    if((dpGetConfig().sys_flags&dpE_SysDelayedLink)==0) {
        for(size_t si=0; si<num_sections; ++si) {
            if(!partialLink(si)) {
                return false;
            }
        }
    }
    else {
        m_symbols.enablePartialLink(true);
    }
    return true;
}

bool dpObjFile::partialLink(size_t si)
{
    LinkData &ld = m_linkdata[si];
    if((ld.flags & dpE_NeedsLink)==0) { return true; }
    ld.flags &= ~dpE_NeedsLink;

    bool ret = true;

    size_t ImageBase = (size_t)(m_data);

    if(si < m_SectionCount) {
        IMAGE_SECTION_HEADER &sect = m_pSectionHeader[si];
        size_t SectionBase = (size_t)(ImageBase + (int)sect.PointerToRawData);
        dpPrintDetail("partial link %s SECT%X \"%s\"\n", getPath(), si, sect.Name);

        DWORD NumRelocations = sect.NumberOfRelocations;
        DWORD FirstRelocation = 0;
        // NumberOfRelocations==0xffff の場合、最初の IMAGE_RELOCATION に実際の値が入っている。(NumberOfRelocations は 16bit のため)
        if(sect.NumberOfRelocations==0xffff && (sect.Characteristics&IMAGE_SCN_LNK_NRELOC_OVFL)!=0) {
            NumRelocations = ((PIMAGE_RELOCATION)(ImageBase + (int)sect.PointerToRelocations))[0].RelocCount;
            FirstRelocation = 1;
        }

        PIMAGE_RELOCATION pRelocation = (PIMAGE_RELOCATION)(ImageBase + (int)sect.PointerToRelocations);
        for(size_t ri=FirstRelocation; ri<NumRelocations; ++ri) {
            RelocationData &reloc = m_relocdata[ri];
            PIMAGE_RELOCATION pReloc = pRelocation + ri;
            TIMAGE_SYMBOL rsym(m_pSymbolTable + m_SymSize*pReloc->SymbolTableIndex, m_StringTable, m_IsBigobj);
            size_t rdata = 0;
            if(rsym.Name[0]=='.' || rsym.Name[0]=='$') {
                rdata = (size_t)(ImageBase + (int)m_pSectionHeader[rsym.SectionNumber-1].PointerToRawData + rsym.Value);
            }
            else {
                rdata = (size_t)resolveSymbol(rsym.Name);
            }
            if(rdata==0) {
                dpPrintError("symbol \"%s\" (referenced by \"%s\") cannot be resolved.\n", rsym.Name, m_path.c_str());
                ret = false;
                continue;
            }

            enum {
#ifdef _M_X64
                IMAGE_SECTION   = IMAGE_REL_AMD64_SECTION,
                IMAGE_SECREL    = IMAGE_REL_AMD64_SECREL,
                IMAGE_REL32     = IMAGE_REL_AMD64_REL32,
                IMAGE_DIR32     = IMAGE_REL_AMD64_ADDR32,
                IMAGE_DIR32NB   = IMAGE_REL_AMD64_ADDR32NB,
#else
                IMAGE_SECTION   = IMAGE_REL_I386_SECTION,
                IMAGE_SECREL    = IMAGE_REL_I386_SECREL,
                IMAGE_REL32     = IMAGE_REL_I386_REL32,
                IMAGE_DIR32     = IMAGE_REL_I386_DIR32,
                IMAGE_DIR32NB   = IMAGE_REL_I386_DIR32NB,
#endif
            };
            size_t addr = SectionBase + pReloc->VirtualAddress;
            if(ld.flags & dpE_NeedsBase) {
                reloc.base = *(DWORD*)(addr);
            }

            // IMAGE_RELOCATION::Type に応じて再配置
            switch(pReloc->Type) {
            case IMAGE_SECTION: break; // 
            case IMAGE_SECREL:  break; // デバッグ情報にしか出てこない (はず)
            case IMAGE_REL32:
                {
                    DWORD rel = (DWORD)(rdata - SectionBase - pReloc->VirtualAddress - 0x04);
                    *(DWORD*)(addr) = (DWORD)(reloc.base + rel);
                }
                break;
            case IMAGE_DIR32:
                {
                    *(DWORD*)(addr) = (DWORD)(reloc.base + rdata);
                }
                break;
            case IMAGE_DIR32NB:
                {
                    *(DWORD*)(addr) = (DWORD)rdata;
                }
                break;
#ifdef _M_X64
            case IMAGE_REL_AMD64_REL32_1:
                {
                    DWORD rel = (DWORD)(rdata - SectionBase - pReloc->VirtualAddress - 0x05);
                    *(DWORD*)(addr) = (DWORD)(reloc.base + rel);
                }
                break;
            case IMAGE_REL_AMD64_REL32_2:
                {
                    DWORD rel = (DWORD)(rdata - SectionBase - pReloc->VirtualAddress - 0x06);
                    *(DWORD*)(addr) = (DWORD)(reloc.base + rel);
                }
                break;
            case IMAGE_REL_AMD64_REL32_3:
                {
                    DWORD rel = (DWORD)(rdata - SectionBase - pReloc->VirtualAddress - 0x07);
                    *(DWORD*)(addr) = (DWORD)(reloc.base + rel);
                }
                break;
            case IMAGE_REL_AMD64_REL32_4:
                {
                    DWORD rel = (DWORD)(rdata - SectionBase - pReloc->VirtualAddress - 0x08);
                    *(DWORD*)(addr) = (DWORD)(reloc.base + rel);
                }
                break;
            case IMAGE_REL_AMD64_REL32_5:
                {
                    DWORD rel = (DWORD)(rdata - SectionBase - pReloc->VirtualAddress - 0x09);
                    *(DWORD*)(addr) = (DWORD)(reloc.base + rel);
                }
                break;
            case IMAGE_REL_AMD64_ADDR64:
                {
                    *(QWORD*)(addr) = (QWORD)(reloc.base + rdata);
                }
                break;
#endif // _M_X64
            default:
                dpPrintWarning("unknown IMAGE_RELOCATION::Type 0x%x\n", pReloc->Type);
                break;
            }
        }

        ld.flags &= ~dpE_NeedsBase;
    }

    if(!ret) {
        ld.flags |= dpE_NeedsLink;
        eachSymbols([&](dpSymbol *sym){
            if(sym->section==si) { sym->flags|=dpE_LinkFailed; }
        });
    }
    return ret;
}


bool dpObjFile::callHandler( dpEventType e )
{
    switch(e) {
    case dpE_OnLoad:   dpCallOnLoadHandler(this);   return true;
    case dpE_OnUnload: dpCallOnUnloadHandler(this); return true;
    }
    return false;
}

dpSymbolTable& dpObjFile::getSymbolTable()            { return m_symbols; }
const char*    dpObjFile::getPath() const             { return m_path.c_str(); }
dpTime         dpObjFile::getLastModifiedTime() const { return m_mtime; }
dpFileType     dpObjFile::getFileType() const         { return FileType; }
void*          dpObjFile::getBaseAddress() const      { return m_data; }

void* dpObjFile::resolveSymbol( const char *name )
{
    if(dpGetLoader()->doesForceHostSymbol(name)) {
        if(dpSymbol *s=dpGetLoader()->findHostSymbolByName(name)) {
            return s->address;
        }
    }
    if(dpSymbolFilters *filts=dpGetFilters()) {
        if(filts->getFilter(getPath())->matchLinkToLocal(name)) {
            if(void *p=resolveSymbolFromLocal(name)) {
                return p;
            }
        }
    }

    if(dpSymbol *s = dpGetLoader()->findHostSymbolByName(name)) {
        return s->address;
    }
    return resolveSymbolFromLocal(name);
}

void* dpObjFile::resolveSymbolFromLocal( const char *name )
{
    if(const dpSymbol *s=getSymbolTable().findSymbolByName(name)) {
        return s->address;
    }
    if(const dpSymbol *s=dpGetLoader()->findSymbolByName(name)) {
        return s->address;
    }
    if(const dpSymbol *s=dpGetLoader()->findHostSymbolByName(name)) {
        return s->address;
    }
    return nullptr;
}

#endif // dpWithObjFile
