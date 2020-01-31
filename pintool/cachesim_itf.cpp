#include "pin.H"
#include "pinMarker.h"
#include <iostream>
#include <fstream>
// #include <stdlib.h>

extern "C"
{
#include "backend.h"
}

Cache* firstLevel;

KNOB<bool> KnobFollowCalls(KNOB_MODE_WRITEONCE, "pintool",
    "follow_calls", "0", "specify if the instrumentation has to follow function calls between the markers");

ADDRINT startCall;
ADDRINT startIns;
ADDRINT stopCall;
ADDRINT stopIns;


LOCALFUN VOID activate()
{
    std::cerr << "activate" << std::endl;
    _pinMarker_active = true;
}
LOCALFUN VOID deactivate()
{
    std::cerr << "deactivate" << std::endl;
    _pinMarker_active = false;
}

VOID ImageLoad(IMG img, VOID *v)
{
    if (IMG_IsMainExecutable(img))
    {
        for( SYM sym= IMG_RegsymHead(img); SYM_Valid(sym); sym = SYM_Next(sym) )
        {
            if (PIN_UndecorateSymbolName ( SYM_Name(sym), UNDECORATION_NAME_ONLY) == "_magic_pin_start")
            {
                startCall = SYM_Address(sym);
            }
            if (PIN_UndecorateSymbolName ( SYM_Name(sym), UNDECORATION_NAME_ONLY) == "_magic_pin_stop")
            {
                stopCall = SYM_Address(sym);
            }
        }

        for( SEC sec= IMG_SecHead(img); SEC_Valid(sec); sec = SEC_Next(sec) )
        {
                for( RTN rtn= SEC_RtnHead(sec); RTN_Valid(rtn); rtn = RTN_Next(rtn) )
                {
                    RTN_Open(rtn);

                    for (INS ins = RTN_InsHead(rtn); INS_Valid(ins); ins = INS_Next(ins))
                    {
                        if (INS_IsDirectControlFlow(ins))
                        {
                            if (INS_DirectControlFlowTargetAddress(ins) == startCall)
                            {
                                if (KnobFollowCalls)
                                {
                                    const AFUNPTR Activate = (AFUNPTR) activate;
                                    INS_InsertPredicatedCall(ins, IPOINT_BEFORE, Activate, IARG_END);
                                }
                                else
                                {
                                    startIns = INS_Address(ins);
                                }
                            }
                            if (INS_DirectControlFlowTargetAddress(ins) == stopCall)
                            {
                                if (KnobFollowCalls)
                                {
                                    const AFUNPTR Deactivate = (AFUNPTR) deactivate;
                                    INS_InsertPredicatedCall(ins, IPOINT_BEFORE, Deactivate, IARG_END);
                                }
                                else
                                {
                                    stopIns = INS_Address(ins);
                                }
                            }
                        }
                    }
                    RTN_Close(rtn);
                }
        }
    }
}

LOCALFUN VOID MemRead_check(UINT64 addr, UINT32 size)
{
    if (_pinMarker_active)
        Cache__load(firstLevel, {addr, size});
}

LOCALFUN VOID MemRead(UINT64 addr, UINT32 size)
{
    Cache__load(firstLevel, {addr, size});
}

LOCALFUN VOID MemWrite_check(UINT64 addr, UINT32 size)
{
    if (_pinMarker_active)
        Cache__store(firstLevel, {addr, size},0);
}

LOCALFUN VOID MemWrite(UINT64 addr, UINT32 size)
{
    Cache__store(firstLevel, {addr, size},0);
}

VOID Instruction(INS ins, VOID *v)
{
    if (KnobFollowCalls)
    {
        const AFUNPTR readFun = (AFUNPTR) MemRead_check;
        const AFUNPTR writeFun = (AFUNPTR) MemWrite_check;
        
        if (INS_IsMemoryRead(ins) && INS_IsStandardMemop(ins))
        {
            INS_InsertPredicatedCall(
                ins, IPOINT_BEFORE, readFun,
                IARG_MEMORYREAD_EA,
                IARG_MEMORYREAD_SIZE,
                IARG_END);
        }

        if(INS_IsMemoryWrite(ins))
        {
            INS_InsertPredicatedCall(
                ins, IPOINT_BEFORE, writeFun,
                IARG_MEMORYWRITE_EA,
                IARG_MEMORYWRITE_SIZE,
                IARG_END);
        }
    }
    else if(INS_Address(ins) > startIns && INS_Address(ins) < stopIns)
    {
        const AFUNPTR readFun = (AFUNPTR) MemRead;
        const AFUNPTR writeFun = (AFUNPTR) MemWrite;
        
        if (INS_IsMemoryRead(ins) && INS_IsStandardMemop(ins))
        {
            INS_InsertPredicatedCall(
                ins, IPOINT_BEFORE, readFun,
                IARG_MEMORYREAD_EA,
                IARG_MEMORYREAD_SIZE,
                IARG_END);
        }

        if(INS_IsMemoryWrite(ins))
        {
            INS_InsertPredicatedCall(
                ins, IPOINT_BEFORE, writeFun,
                IARG_MEMORYWRITE_EA,
                IARG_MEMORYWRITE_SIZE,
                IARG_END);
        }
    }
}

VOID printStats(Cache* cache)
{
    std::cout << std::string(cache->name) << "\n";
    std::cout << "LOAD: " << cache->LOAD.count << " size: " << cache->LOAD.byte << "B\n";
    std::cout << "STORE: " << cache->STORE.count << " size: " << cache->STORE.byte << "B\n";
    std::cout << "HIT: " << cache->HIT.count << " size: " << cache->HIT.byte << "B\n";
    std::cout << "MISS: " << cache->MISS.count << " size: " << cache->MISS.byte << "B\n";
    std::cout << "EVICT: " << cache->EVICT.count << " size: " << cache->EVICT.byte << "B\n";
    std::cout << "\n";
    if (cache->load_from != NULL)
        printStats(cache->load_from);
}

VOID Fini(int code, VOID * v)
{
    printStats(firstLevel);
    dealloc_cacheSim(firstLevel);
}

int main(int argc, char *argv[])
{
    std::cout << "starting" << std::endl;

    std::ifstream in("cachedef");
    int num;
    in >> num;
    std::string* lines = new std::string[num];
    for (int i = 0; i < num; ++i)
    {
        while (in.peek() == '#' || in.peek() == '\n') std::getline(in, lines[i]);
        std::getline(in, lines[i]);
    }

    // firstLevel = get_cacheSim_from_file("cachedef"); //TODO check if this works
    int num = get_cacheSim_from_file("cachedef"); //TODO check if this works

    std::cout << num << std::endl;
    std::cout << "init sym" << std::endl;
    PIN_InitSymbols();

    // Initialize PIN library. Print help message if -h(elp) is specified
    // in the command line or the command line is invalid 
    std::cout << "init pin" << std::endl;
    if( PIN_Init(argc,argv) )
    {
        return 1;
    }

    // if (KnobFollowCalls)
    // {
    //     std::cerr << "follow calls" << std::endl;
    // }

    std::cout << "ins image" << std::endl;
    IMG_AddInstrumentFunction(ImageLoad, 0);
    std::cout << "ins ins" << std::endl;
    INS_AddInstrumentFunction(Instruction, 0);

    std::cout << "add fini" << std::endl;
    PIN_AddFiniFunction(Fini, 0);
    
    // std::cerr << "starting\n" << std::endl;
    // Start the program, never returns
    std::cout << "start" << std::endl;
    PIN_StartProgram();
    
    return 0;
}