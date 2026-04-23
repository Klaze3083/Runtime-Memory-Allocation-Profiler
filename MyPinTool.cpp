#include "pin.H"
#include <iostream>
#include <fstream>
#include <map>
#include <string>

using std::cerr;
using std::endl;
using std::map;
using std::ofstream;
using std::string;

/* ============================================================ */
// OUTPUT
/* ============================================================ */

ofstream outFile;

/* ============================================================ */
// THREAD SAFETY
/* ============================================================ */

PIN_LOCK lock;

/* ============================================================ */
// DATA STRUCTURES
/* ============================================================ */

struct AllocInfo
{
    size_t size;
    string funcName;
};

map<ADDRINT, AllocInfo> activeAllocs;
map<string, size_t> totalMemPerFunc;
map<string, size_t> allocCountPerFunc;

/* ============================================================ */
// ORIGINAL FUNCTION POINTERS
/* ============================================================ */

typedef VOID *(*malloc_t)(size_t);
typedef VOID *(*calloc_t)(size_t, size_t);
typedef VOID *(*realloc_t)(VOID *, size_t);
typedef VOID (*free_t)(VOID *);

malloc_t real_malloc = NULL;
calloc_t real_calloc = NULL;
realloc_t real_realloc = NULL;
free_t real_free = NULL;

/* ============================================================ */
// UTILITY
/* ============================================================ */

string GetFuncName(ADDRINT ip)
{
    PIN_LockClient();
    RTN rtn = RTN_FindByAddress(ip);
    string name = "UNKNOWN";
    if (RTN_Valid(rtn))
        name = RTN_Name(rtn);
    PIN_UnlockClient();
    return name;
}

/* ============================================================ */
// WRAPPERS
/* ============================================================ */

VOID *MyMalloc(size_t size, ADDRINT ip)
{
    VOID *ret = real_malloc(size);
    if (!ret)
        return ret;

    PIN_GetLock(&lock, 1);

    string func = GetFuncName(ip);

    activeAllocs[(ADDRINT)ret] = {size, func};
    totalMemPerFunc[func] += size;
    allocCountPerFunc[func]++;

    outFile << "[ALLOC] Addr: " << std::hex << (ADDRINT)ret
            << " Size: " << std::dec << size
            << " Func: " << func << endl;

    PIN_ReleaseLock(&lock);
    return ret;
}

VOID *MyCalloc(size_t nmemb, size_t size, ADDRINT ip)
{
    VOID *ret = real_calloc(nmemb, size);
    if (!ret)
        return ret;

    PIN_GetLock(&lock, 1);

    size_t total = nmemb * size;
    string func = GetFuncName(ip);

    activeAllocs[(ADDRINT)ret] = {total, func};
    totalMemPerFunc[func] += total;
    allocCountPerFunc[func]++;

    outFile << "[CALLOC] Addr: " << std::hex << (ADDRINT)ret
            << " Size: " << std::dec << total
            << " Func: " << func << endl;

    PIN_ReleaseLock(&lock);
    return ret;
}

VOID *MyRealloc(VOID *ptr, size_t size, ADDRINT ip)
{
    VOID *ret = real_realloc(ptr, size);

    PIN_GetLock(&lock, 1);

    if (ret != NULL && ptr != NULL)
    {
        auto it = activeAllocs.find((ADDRINT)ptr);
        if (it != activeAllocs.end())
            activeAllocs.erase(it);
    }

    if (ret != NULL)
    {
        string func = GetFuncName(ip);

        activeAllocs[(ADDRINT)ret] = {size, func};
        totalMemPerFunc[func] += size;
        allocCountPerFunc[func]++;

        outFile << "[REALLOC] Addr: " << std::hex << (ADDRINT)ret
                << " Size: " << std::dec << size
                << " Func: " << func << endl;
    }

    PIN_ReleaseLock(&lock);
    return ret;
}

VOID MyFree(VOID *ptr)
{
    PIN_GetLock(&lock, 1);

    auto it = activeAllocs.find((ADDRINT)ptr);

    if (it != activeAllocs.end())
    {
        outFile << "[FREE ] Addr: " << std::hex << (ADDRINT)ptr
                << " Size: " << std::dec << it->second.size
                << " Func: " << it->second.funcName << endl;

        activeAllocs.erase(it);
    }

    PIN_ReleaseLock(&lock);

    real_free(ptr);
}

/* ============================================================ */
// IMAGE LOAD
/* ============================================================ */

VOID ImageLoad(IMG img, VOID *v)
{
    cerr << "Loaded Image: " << IMG_Name(img) << endl;

    /* malloc */
    RTN mallocRtn = RTN_FindByName(img, "malloc");
    if (RTN_Valid(mallocRtn))
    {
        cerr << "Replacing malloc in: " << IMG_Name(img) << endl;

        PROTO proto = PROTO_Allocate(
            PIN_PARG(void *), CALLINGSTD_DEFAULT,
            "malloc",
            PIN_PARG(size_t),
            PIN_PARG_END());

        real_malloc = (malloc_t)RTN_ReplaceSignature(
            mallocRtn, AFUNPTR(MyMalloc),
            IARG_PROTOTYPE, proto,
            IARG_FUNCARG_ENTRYPOINT_VALUE, 0,
            IARG_RETURN_IP,
            IARG_END);

        PROTO_Free(proto);
    }

    /* calloc */
    RTN callocRtn = RTN_FindByName(img, "calloc");
    if (!RTN_Valid(callocRtn))
        callocRtn = RTN_FindByName(img, "__libc_calloc");

    if (RTN_Valid(callocRtn))
    {
        cerr << "Replacing calloc in: " << IMG_Name(img) << endl;

        PROTO proto = PROTO_Allocate(
            PIN_PARG(void *), CALLINGSTD_DEFAULT,
            "calloc",
            PIN_PARG(size_t),
            PIN_PARG(size_t),
            PIN_PARG_END());

        real_calloc = (calloc_t)RTN_ReplaceSignature(
            callocRtn, AFUNPTR(MyCalloc),
            IARG_PROTOTYPE, proto,
            IARG_FUNCARG_ENTRYPOINT_VALUE, 0,
            IARG_FUNCARG_ENTRYPOINT_VALUE, 1,
            IARG_RETURN_IP,
            IARG_END);

        PROTO_Free(proto);
    }

    /* realloc */
    RTN reallocRtn = RTN_FindByName(img, "realloc");
    if (RTN_Valid(reallocRtn))
    {
        cerr << "Replacing realloc in: " << IMG_Name(img) << endl;

        PROTO proto = PROTO_Allocate(
            PIN_PARG(void *), CALLINGSTD_DEFAULT,
            "realloc",
            PIN_PARG(void *),
            PIN_PARG(size_t),
            PIN_PARG_END());

        real_realloc = (realloc_t)RTN_ReplaceSignature(
            reallocRtn, AFUNPTR(MyRealloc),
            IARG_PROTOTYPE, proto,
            IARG_FUNCARG_ENTRYPOINT_VALUE, 0,
            IARG_FUNCARG_ENTRYPOINT_VALUE, 1,
            IARG_RETURN_IP,
            IARG_END);

        PROTO_Free(proto);
    }

    /* free */
    RTN freeRtn = RTN_FindByName(img, "free");
    if (RTN_Valid(freeRtn))
    {
        cerr << "Replacing free in: " << IMG_Name(img) << endl;

        PROTO proto = PROTO_Allocate(
            PIN_PARG(void), CALLINGSTD_DEFAULT,
            "free",
            PIN_PARG(void *),
            PIN_PARG_END());

        real_free = (free_t)RTN_ReplaceSignature(
            freeRtn, AFUNPTR(MyFree),
            IARG_PROTOTYPE, proto,
            IARG_FUNCARG_ENTRYPOINT_VALUE, 0,
            IARG_END);

        PROTO_Free(proto);
    }
}

/* ============================================================ */
// FINAL REPORT
/* ============================================================ */

VOID Fini(INT32 code, VOID *v)
{
    outFile << "\n=========== FINAL REPORT ===========\n";

    outFile << "\n-- Allocation Summary Per Function --\n";
    for (auto &p : totalMemPerFunc)
    {
        outFile << "Function: " << p.first
                << " | Total Bytes: " << p.second
                << " | Alloc Count: " << allocCountPerFunc[p.first]
                << endl;
    }

    outFile << "\n-- Active Allocations (Leaks) --\n";
    for (auto &p : activeAllocs)
    {
        outFile << "Leaked Addr: " << std::hex << p.first
                << " Size: " << std::dec << p.second.size
                << " Func: " << p.second.funcName
                << endl;
    }

    outFile << "\n====================================\n";
}

/* ============================================================ */
// MAIN
/* ============================================================ */

int main(int argc, char *argv[])
{
    PIN_InitSymbols();

    if (PIN_Init(argc, argv))
    {
        cerr << "PIN Init failed\n";
        return -1;
    }

    PIN_InitLock(&lock);

    outFile.open("mem_report.out");
    if (!outFile.is_open())
    {
        cerr << "Error opening output file\n";
        return -1;
    }

    IMG_AddInstrumentFunction(ImageLoad, 0);
    PIN_AddFiniFunction(Fini, 0);

    cerr << "Running Memory Allocation Pintool...\n";

    PIN_StartProgram();

    return 0;
}