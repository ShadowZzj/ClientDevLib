set(PROJECT_NAME GHInjectorLibrary)
set(CMAKE_CXX_STANDARD 17)
################################################################################
# Source groups
################################################################################
set(Headerdateien
    "Download Manager.h"
    "Eject.h"
    "Error.h"
    "Handle Hijacking.h"
    "Hook Scanner.h"
    "Import Handler.h"
    "Injection.h"
    "pch.h"
    "Process Info.h"
    "resource.h"
    "Start Routine.h"
    "Symbol Loader.h"
    "Symbol Parser.h"
    "Tools.h"
)
source_group("Headerdateien" FILES ${Headerdateien})

set(Headerdateien__Injection_Methods
    "Injection Internal.h"
    "Manual Mapping Internal.h"
    "Manual Mapping.h"
    "VEH Shell.h"
)
source_group("Headerdateien\\Injection Methods" FILES ${Headerdateien__Injection_Methods})

set(Headerdateien__NT
    "NT Defs.h"
    "NT Funcs.h"
    "Win10.h"
    "Win11.h"
    "Win7.h"
    "Win8.h"
    "Win81.h"
)
source_group("Headerdateien\\NT" FILES ${Headerdateien__NT})

set(Quelldateien
    "Download Manager.cpp"
    "Eject.cpp"
    "Handle Hijacking.cpp"
    "main.cpp"
    "pch.cpp"
    "Process Info.cpp"
    "Symbol Loader.cpp"
    "Symbol Parser.cpp"
    "Tools.cpp"
)
source_group("Quelldateien" FILES ${Quelldateien})

set(Quelldateien__native
    "Hook Scanner.cpp"
    "Import Handler.cpp"
    "Injection.cpp"
    "Start Routine.cpp"
)
source_group("Quelldateien\\native" FILES ${Quelldateien__native})

set(Quelldateien__native__Injection_Methods
    "Injection Generic.cpp"
    "Manual Mapping.cpp"
    "VEH Shell.cpp"
)
source_group("Quelldateien\\native\\Injection Methods" FILES ${Quelldateien__native__Injection_Methods})

set(Quelldateien__native__Start_Routine_Methods
    "FakeVEH.cpp"
    "KernelCallback.cpp"
    "NtCreateThreadEx.cpp"
    "QueueUserAPC.cpp"
    "SetWindowsHookEx.cpp"
    "Thread Hijacking.cpp"
)
source_group("Quelldateien\\native\\Start Routine Methods" FILES ${Quelldateien__native__Start_Routine_Methods})

set(Quelldateien__wow64
    "Hook Scanner WOW64.cpp"
    "Import Handler WOW64.cpp"
    "Start Routine WOW64.cpp"
)
source_group("Quelldateien\\wow64" FILES ${Quelldateien__wow64})

set(Quelldateien__wow64__Injection_Methods
    "Injection Generic WOW64.cpp"
    "Manual Mapping WOW64.cpp"
    "WOW64 Shells.h"
)
source_group("Quelldateien\\wow64\\Injection Methods" FILES ${Quelldateien__wow64__Injection_Methods})

set(Quelldateien__wow64__Start_Routine_Methods
    "FakeVEH WOW64.cpp"
    "KernelCallback WOW64.cpp"
    "NtCreateThreadEx WOW64.cpp"
    "QueueUserAPC WOW64.cpp"
    "SetWindowsHookEx WOW64.cpp"
    "Thread Hijacking WOW64.cpp"
)
source_group("Quelldateien\\wow64\\Start Routine Methods" FILES ${Quelldateien__wow64__Start_Routine_Methods})

set(ALL_FILES
    ${Headerdateien}
    ${Headerdateien__Injection_Methods}
    ${Headerdateien__NT}
    ${Quelldateien}
    ${Quelldateien__native}
    ${Quelldateien__native__Injection_Methods}
    ${Quelldateien__native__Start_Routine_Methods}
    ${Quelldateien__wow64}
    ${Quelldateien__wow64__Injection_Methods}
    ${Quelldateien__wow64__Start_Routine_Methods}
)

################################################################################
# Target
################################################################################
add_library(${PROJECT_NAME} SHARED ${ALL_FILES})

set_property(TARGET  ${PROJECT_NAME}  ${PROJECT_NAME} PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")