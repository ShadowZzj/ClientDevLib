include_guard(DIRECTORY)

# Match UTF-8 source files regardless of the developer machine's ANSI code page.
# MASM and resource compiler invocations must not receive this MSVC option.
if(MSVC)
    add_compile_options("$<$<COMPILE_LANGUAGE:C,CXX>:/utf-8>")
endif()
