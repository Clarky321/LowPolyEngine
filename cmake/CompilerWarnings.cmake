# Warning policy for first-party targets.
#
# Applied per target instead of globally so third-party code pulled in through
# Conan is not compiled with our stricter flags. Dependency headers come in as
# SYSTEM/external includes and do not produce warnings here.

function(lpe_set_project_warnings target)
    set(msvc_warnings
        /W4
        /permissive-       # standard-conforming name lookup and conversions
        /w14242            # possible loss of data in conversion
        /w14254            # larger bit field assigned to smaller one
        /w14263            # member function hides a base virtual function
        /w14265            # class has virtual functions but non-virtual destructor
        /w14287            # unsigned/negative constant mismatch
        /w14296            # expression is always true/false
        /w14311            # pointer truncation
        /w14826            # sign-extending conversion
        /w14905            # wide string literal cast to LPSTR
        /w14906            # string literal cast to LPWSTR
        /w14928            # illegal copy-initialization
    )

    set(gcc_clang_warnings
        -Wall
        -Wextra
        -Wpedantic
        -Wshadow
        -Wconversion
        -Wsign-conversion
        -Wnon-virtual-dtor
        -Wold-style-cast
        -Woverloaded-virtual
        -Wnull-dereference
        -Wdouble-promotion
        -Wformat=2
        -Wimplicit-fallthrough
    )

    if(LPE_WARNINGS_AS_ERRORS)
        list(APPEND msvc_warnings /WX)
        list(APPEND gcc_clang_warnings -Werror)
    endif()

    if(MSVC)
        target_compile_options(${target} PRIVATE ${msvc_warnings})
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${target} PRIVATE ${gcc_clang_warnings})
    else()
        message(WARNING "No warning policy defined for compiler '${CMAKE_CXX_COMPILER_ID}'")
    endif()
endfunction()
