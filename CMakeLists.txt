# ===========================================================================
# wimlib — build definition.
#
# Reads the Windows Imaging Format: install.wim and install.esd, the two files
# an ISO of a Windows installer is mostly made of. libarchive has no WIM reader
# — its formats are 7zip, ar, cab, cpio, iso9660, lha, mtree, rar, rar5, raw,
# tar, warc, xar and zip — so this is the third rung of the same ladder
# libudfread is the second of: whichever reader the image in front of us needs.
#
# The licence is conditional, and the condition is enforced by this file.
#
# libwim is offered under a dual licence (upstream's COPYING): GPLv3+ for the
# whole project, and LGPL 2.1+ for the library alone — but the LGPL option is
# withdrawn "when prohibited by a third-party software license", and upstream
# names the case outright: a build linked against libntfs-3g, which is GPL, can
# only be GPL. Nostalgia Commander is a closed binary and cannot carry GPL code
# at all, so WITH_NTFS_3G is not a feature switch here. It is the condition on
# which we may use the library, which is why nothing below offers a way to turn
# it on, and why neither ntfs-3g source file is in the source list.
#
# The version matters for that licence, so do not treat the pin as routine: the
# library was LGPLv3+ up to and including 1.14.4, and 1.14.5 relaxed it to
# LGPL 2.1+ ("The license for the library has been relaxed", NEWS.md). We are on
# 1.14.5, which is why this component ends up under exactly libudfread's terms
# rather than v3's. Re-read COPYING on any version change, in either direction.
#
# FUSE is off for a plainer reason: mounting a WIM image is not something a file
# manager does — we read entries out of it — and libfuse would be another
# non-permissive dependency for a feature nobody asked for.
#
# With those two off, the arrangement is exactly libudfread's — same licence, same
# clause, same shape — and that file carries the full reasoning: built shared,
# versioned as upstream versions it, shipped beside the application, its symbols
# bound by the app at run time so a missing or replaced copy costs WIM support
# instead of stopping the program.
#
# we fork it rather than patch it — and the difference is the whole point.
# What gets built is a submodule like any other, checked out at a commit; that
# commit simply lives in our fork rather than upstream's repository, because
# upstream cannot build against the Windows sdk (the POSIX-header and off_t notes
# below). Nothing here applies a diff at build time, and there is no modified copy
# generated on the side as libudfread has.
#
# the licence obligation is unchanged and still satisfied — better, if anything.
# LGPL 2.1 asks that whoever receives this binary can get the library's source and
# replace it; the answer is now "here is our fork, at this commit", which is the
# modified source rather than the pristine upstream it was built from. That is
# what the licence actually asks for. The fork keeps upstream's copying intact and
# WITH_NTFS_3G stays off, since that is the condition the LGPL option rests on.
#
# Upstream builds with autotools and ships no CMakeLists.txt, so the build is
# ours. It lives in the fork rather than in the application's tree because that
# is what makes the fork the complete answer to a source request: clone it and
# it builds, with no file from our repository needed to do it. The source list
# mirrors libwim_la_SOURCES in upstream's Makefile.am, the config.h checks mirror
# configure.ac, and the shared-library version is computed from the same
# -version-info line libtool reads. If any of those change on a version bump,
# they are what to re-read; two of the three fail loudly on their own.
#
# This is the one commit here upstream would never take. Every other change in
# the fork is a fix worth sending to the forums; this file is not, so leave it
# out of any `git format-patch` that goes upstream.
# ===========================================================================
cmake_minimum_required(VERSION 3.21)
project(wimlib LANGUAGES C)

include(CheckCSourceCompiles)
include(CheckFunctionExists)
include(CheckIncludeFile)
include(CheckStructHasMember)

# Set by the application's dependency build too, and repeated for a standalone
# clone; find_package is idempotent, so the parent's result simply stands.
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

set(NCOMM_WIMLIB_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
set(NCOMM_WIMLIB_GEN "${CMAKE_CURRENT_BINARY_DIR}")

if(NOT EXISTS "${NCOMM_WIMLIB_DIR}/src/wim.c")
    message(FATAL_ERROR
        "3rdparty/wimlib is empty — the submodule has not been checked out.\n"
        "Run:  git submodule update --init --recursive")
endif()

# --- Can this compiler build it at all? -------------------------------------
# wimlib is gnu c, not portable C, and says so: "Compiler-specific definitions.
# Currently, only GCC and clang are supported." Its min()/max() are statement
# expressions over typeof, which MSVC has never implemented and will not parse —
# so this is not a matter of finding the right flags, and no amount of CMake
# fixes it. The probe below is the two constructs themselves, which is the only
# honest way to ask the question: a compiler that swallows it can build wimlib,
# whatever its name.
#
# NOT a fatal error, because the app is designed to survive the library's
# absence — nothing links it, and a WIM simply stays unopened, the same as on a
# machine where someone deleted the file. A hard failure here would instead take
# out the entire Windows build of a program whose other 99% is unaffected.
#
# the toolchain decision has since been taken — Windows builds with clang now —
# and it was necessary without being sufficient. clang clears this probe on every
# platform, and the Windows build then failed for a second, entirely unrelated
# reason: POSIX headers wimlib includes unguarded. That is fixed in the source
# now, in our fork, rather than worked around here — see the fork note above.
check_c_source_compiles("
int main(void) {
    int a = 1, b = 2;
    return ({ typeof(a) _a = a; typeof(b) _b = b; (_a < _b) ? _a : _b; }) - 1;
}" NCOMM_WIMLIB_COMPILER_IS_GNU_C)

if(NOT NCOMM_WIMLIB_COMPILER_IS_GNU_C)
    message(WARNING
        "Skipping wimlib: ${CMAKE_C_COMPILER_ID} does not support the GNU C "
        "extensions (statement expressions, typeof) its sources are written in. "
        "The prefix will have no libwim, and the application will build and run "
        "without WIM/ESD support.")
    return()
endif()

# --- Version ----------------------------------------------------------------
# Read out of the submodule rather than written here, so a tag bump moves it. The
# string is what wimlib_get_version_string() returns; upstream derives it from
# `git describe` at build time and falls back to this literal in released
# tarballs, which for a checked-out tag is the same answer.
file(STRINGS "${NCOMM_WIMLIB_DIR}/tools/get-version-number.sh" _ncomm_wim_vers
     REGEX "^[ \t]*vers=\"[0-9]")
string(REGEX MATCH "[0-9]+\\.[0-9]+\\.[0-9]+" NCOMM_WIMLIB_VERSION "${_ncomm_wim_vers}")
if(NOT NCOMM_WIMLIB_VERSION)
    message(FATAL_ERROR
        "Could not read wimlib's version out of tools/get-version-number.sh.")
endif()
set(NCOMM_WIMLIB_BUGREPORT "https://wimlib.net/forums/")

# --- Shared-library version -------------------------------------------------
# A shared library's identity is its soname, and matching upstream's is what lets
# someone drop their own build — or a distribution's libwim.so.15 — in place of
# ours, which is the substitutability the LGPL is asking for rather than a
# formality.
#
# The number is NOT the release version and is not derived from it: libtool reads
# `-version-info current:revision:age` and names the library
# (current-age).age.revision, with (current-age) as the soname. Computing it here
# from that same line means a version bump carries the soname with it instead of
# leaving a hardcoded number behind that nothing would contradict until the app
# failed to find the library at run time. ncomm-deps.cmake knows the major number
# too — see the note there.
file(STRINGS "${NCOMM_WIMLIB_DIR}/Makefile.am" _ncomm_wim_vinfo
     REGEX "-version-info[ \t]+[0-9]+:[0-9]+:[0-9]+")
if(NOT _ncomm_wim_vinfo MATCHES "-version-info[ \t]+([0-9]+):([0-9]+):([0-9]+)")
    message(FATAL_ERROR
        "Could not read libwim's -version-info out of wimlib's Makefile.am, so "
        "the shared library's soname cannot be computed. Check whether upstream "
        "has changed how it versions the library.")
endif()
math(EXPR NCOMM_WIMLIB_SOVERSION "${CMAKE_MATCH_1} - ${CMAKE_MATCH_3}")
set(NCOMM_WIMLIB_LIBVERSION "${NCOMM_WIMLIB_SOVERSION}.${CMAKE_MATCH_3}.${CMAKE_MATCH_2}")

# --- config.h ---------------------------------------------------------------
# One check per AC_CHECK_* in upstream's configure.ac, kept complete rather than
# trimmed to what today's sources read: a missing HAVE_ is silent, and costs a
# fast path or a whole capability with nothing to point at.
#
# check_function_exists, not check_symbol_exists, deliberately — AC_CHECK_FUNCS
# probes by linking, with no header and no declaration, and answering the same
# question the same way keeps this file comparable to the configure script it
# replaces (a wrong header guess in check_symbol_exists reads as "absent").
set(CMAKE_REQUIRED_DEFINITIONS -D_GNU_SOURCE) # as the build compiles: glibc hides mempcpy et al without it
foreach(_fn futimens utimensat flock mempcpy openat fstatat readlinkat fdopendir
            posix_fallocate llistxattr lgetxattr fsetxattr lsetxattr getopt_long_only)
    string(TOUPPER "HAVE_${_fn}" _var)
    check_function_exists(${_fn} ${_var})
endforeach()

foreach(_hdr alloca.h byteswap.h endian.h errno.h glob.h machine/endian.h stdarg.h
             stddef.h stdlib.h sys/byteorder.h sys/endian.h sys/file.h sys/syscall.h
             sys/sysctl.h sys/time.h sys/times.h sys/xattr.h time.h unistd.h utime.h)
    string(TOUPPER "HAVE_${_hdr}" _var)
    string(REGEX REPLACE "[/.]" "_" _var "${_var}")
    check_include_file("${_hdr}" ${_var})
endforeach()

check_struct_has_member("struct stat" st_mtim "sys/stat.h"
                        HAVE_STAT_NANOSECOND_PRECISION LANGUAGE C)
unset(CMAKE_REQUIRED_DEFINITIONS)

if(CMAKE_C_BYTE_ORDER STREQUAL "BIG_ENDIAN")
    set(WORDS_BIGENDIAN 1)
endif()

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/config.h.in"
    "${NCOMM_WIMLIB_GEN}/config.h")

# --- The library ------------------------------------------------------------
# shared, explicitly, and NOT via BUILD_SHARED_LIBS — the parent CMakeLists
# forces that off for every other dependency, so the keyword here is what makes
# this one the exception, exactly as it is for libudfread.
#
# The list is libwim_la_SOURCES from upstream's Makefile.am, minus the two
# ntfs-3g files (see the licence note at the top) and minus test_support.c
# (upstream's fuzzing harness, behind --enable-test-support). Upstream also lists
# every private header there, which matters to `make dist` and not to us.
add_library(wim SHARED
    "${NCOMM_WIMLIB_DIR}/src/add_image.c"
    "${NCOMM_WIMLIB_DIR}/src/avl_tree.c"
    "${NCOMM_WIMLIB_DIR}/src/blob_table.c"
    "${NCOMM_WIMLIB_DIR}/src/compress.c"
    "${NCOMM_WIMLIB_DIR}/src/compress_common.c"
    "${NCOMM_WIMLIB_DIR}/src/compress_parallel.c"
    "${NCOMM_WIMLIB_DIR}/src/compress_serial.c"
    "${NCOMM_WIMLIB_DIR}/src/cpu_features.c"
    "${NCOMM_WIMLIB_DIR}/src/decompress.c"
    "${NCOMM_WIMLIB_DIR}/src/decompress_common.c"
    "${NCOMM_WIMLIB_DIR}/src/delete_image.c"
    "${NCOMM_WIMLIB_DIR}/src/dentry.c"
    "${NCOMM_WIMLIB_DIR}/src/divsufsort.c"
    "${NCOMM_WIMLIB_DIR}/src/encoding.c"
    "${NCOMM_WIMLIB_DIR}/src/error.c"
    "${NCOMM_WIMLIB_DIR}/src/export_image.c"
    "${NCOMM_WIMLIB_DIR}/src/extract.c"
    "${NCOMM_WIMLIB_DIR}/src/file_io.c"
    "${NCOMM_WIMLIB_DIR}/src/header.c"
    "${NCOMM_WIMLIB_DIR}/src/inode.c"
    "${NCOMM_WIMLIB_DIR}/src/inode_fixup.c"
    "${NCOMM_WIMLIB_DIR}/src/inode_table.c"
    "${NCOMM_WIMLIB_DIR}/src/integrity.c"
    "${NCOMM_WIMLIB_DIR}/src/iterate_dir.c"
    "${NCOMM_WIMLIB_DIR}/src/join.c"
    "${NCOMM_WIMLIB_DIR}/src/lcpit_matchfinder.c"
    "${NCOMM_WIMLIB_DIR}/src/lzms_common.c"
    "${NCOMM_WIMLIB_DIR}/src/lzms_compress.c"
    "${NCOMM_WIMLIB_DIR}/src/lzms_decompress.c"
    "${NCOMM_WIMLIB_DIR}/src/lzx_common.c"
    "${NCOMM_WIMLIB_DIR}/src/lzx_compress.c"
    "${NCOMM_WIMLIB_DIR}/src/lzx_decompress.c"
    "${NCOMM_WIMLIB_DIR}/src/metadata_resource.c"
    "${NCOMM_WIMLIB_DIR}/src/mount_image.c"
    "${NCOMM_WIMLIB_DIR}/src/pathlist.c"
    "${NCOMM_WIMLIB_DIR}/src/paths.c"
    "${NCOMM_WIMLIB_DIR}/src/pattern.c"
    "${NCOMM_WIMLIB_DIR}/src/progress.c"
    "${NCOMM_WIMLIB_DIR}/src/read_path.c"
    "${NCOMM_WIMLIB_DIR}/src/reference.c"
    "${NCOMM_WIMLIB_DIR}/src/registry.c"
    "${NCOMM_WIMLIB_DIR}/src/reparse.c"
    "${NCOMM_WIMLIB_DIR}/src/resource.c"
    "${NCOMM_WIMLIB_DIR}/src/scan.c"
    "${NCOMM_WIMLIB_DIR}/src/security.c"
    "${NCOMM_WIMLIB_DIR}/src/sha1.c"
    "${NCOMM_WIMLIB_DIR}/src/solid.c"
    "${NCOMM_WIMLIB_DIR}/src/split.c"
    "${NCOMM_WIMLIB_DIR}/src/tagged_items.c"
    "${NCOMM_WIMLIB_DIR}/src/template.c"
    "${NCOMM_WIMLIB_DIR}/src/textfile.c"
    "${NCOMM_WIMLIB_DIR}/src/threads.c"
    "${NCOMM_WIMLIB_DIR}/src/timestamp.c"
    "${NCOMM_WIMLIB_DIR}/src/update_image.c"
    "${NCOMM_WIMLIB_DIR}/src/util.c"
    "${NCOMM_WIMLIB_DIR}/src/verify.c"
    "${NCOMM_WIMLIB_DIR}/src/wim.c"
    "${NCOMM_WIMLIB_DIR}/src/write.c"
    "${NCOMM_WIMLIB_DIR}/src/xml.c"
    "${NCOMM_WIMLIB_DIR}/src/xml_windows.c"
    "${NCOMM_WIMLIB_DIR}/src/xmlproc.c"
    "${NCOMM_WIMLIB_DIR}/src/xpress_compress.c"
    "${NCOMM_WIMLIB_DIR}/src/xpress_decompress.c"
)

# The platform half of libwim_la_SOURCES. Windows gets the WIMBoot, vss and
# native-Win32 capture/apply paths (and -lntdll, for the NT-level calls those
# make); everywhere else gets the POSIX pair.
if(WIN32)
    target_sources(wim PRIVATE
        "${NCOMM_WIMLIB_DIR}/src/wimboot.c"
        "${NCOMM_WIMLIB_DIR}/src/win32_common.c"
        "${NCOMM_WIMLIB_DIR}/src/win32_apply.c"
        "${NCOMM_WIMLIB_DIR}/src/win32_capture.c"
        "${NCOMM_WIMLIB_DIR}/src/win32_replacements.c"
        "${NCOMM_WIMLIB_DIR}/src/win32_vss.c")
else()
    target_sources(wim PRIVATE
        "${NCOMM_WIMLIB_DIR}/src/unix_apply.c"
        "${NCOMM_WIMLIB_DIR}/src/unix_capture.c")
endif()

set_target_properties(wim PROPERTIES
    # gnu99, which is upstream's AM_CFLAGS and not a preference: the sources use
    # unnamed structs and unions, a C11 feature GCC and clang offer in gnu99 but
    # not in strict c99.
    C_STANDARD 99
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS ON
    # -fvisibility=hidden, upstream's PLATFORM_CFLAGS. Only what wimlib.h marks
    # wimlibapi is exported; without this the library would also export its whole
    # internal surface, and each of those symbols becomes an accidental promise.
    C_VISIBILITY_PRESET hidden
    VERSION   "${NCOMM_WIMLIB_LIBVERSION}"
    SOVERSION "${NCOMM_WIMLIB_SOVERSION}"
    # macOS: the install name stays @rpath-relative through installation, so the
    # copy inside the .app bundle is found beside the executable rather than at
    # the absolute path of the build machine's dependency prefix.
    INSTALL_NAME_DIR "@rpath")

if(WIN32)
    # CMake versions a .so and a .dylib from version/soversion but leaves a DLL's
    # name alone, so the soname has to go into the filename by hand. libwim-15.dll
    # is what libtool produces and therefore what upstream's own Windows releases
    # carry — the point being that one of those is a drop-in replacement for ours.
    set_target_properties(wim PROPERTIES
        PREFIX "lib"
        OUTPUT_NAME "wim-${NCOMM_WIMLIB_SOVERSION}")
endif()

target_include_directories(wim PRIVATE
    "${NCOMM_WIMLIB_DIR}/include"
    "${NCOMM_WIMLIB_GEN}")   # the generated config.h

# HAVE_CONFIG_H and the large-file trio are upstream's AM_CPPFLAGS; BUILDING_WIMLIB
# is its libwim_la_CFLAGS, and is what turns wimlibapi into a real export marker
# (dllexport / default visibility) instead of nothing. All PRIVATE: a consumer of
# the header that defined BUILDING_WIMLIB would try to export the API a second
# time from its own side.
target_compile_definitions(wim PRIVATE
    HAVE_CONFIG_H
    BUILDING_WIMLIB
    _LARGEFILE_SOURCE
    _FILE_OFFSET_BITS=64
    _GNU_SOURCE)

if(WIN32)
    # Upstream's PLATFORM_CPPFLAGS for a native Windows build. UNICODE/_UNICODE
    # are load-bearing rather than stylistic: wimlib_tchar is wchar_t there, and
    # the whole public API changes shape with it.
    target_compile_definitions(wim PRIVATE
        _POSIX
        _POSIX_THREAD_SAFE_FUNCTIONS
        UNICODE
        _UNICODE
        _CRT_NON_CONFORMING_SWPRINTFS
        # The POSIX names in <io.h> (close, read, write, unlink) are what MinGW
        # gives wimlib as standard, so the sources use them; the ucrt declares the
        # same functions but marks them deprecated in favour of the _-prefixed
        # spellings. That is Microsoft's preference, not a defect, and silencing it
        # here is what keeps the platform-independent sources free of #ifdefs.
        _CRT_NONSTDC_NO_WARNINGS
        _WIN32_WINNT=0x0600)
endif()

# -Wno-pointer-sign is upstream's, and not cosmetic: without it the char/uchar
# boundary in the compression code produces hundreds of warnings that would
# bury anything real. It is keyed on the compiler ID rather than on not MSVC
# because CMake reports MSVC as true for clang-cl — which is a Clang and does
# emit -Wpointer-sign, so the old spelling dropped the flag exactly where it
# was still needed and divsufsort.c started printing them again.
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(wim PRIVATE -Wno-pointer-sign)
endif()

# -fno-common matches upstream; it is the default in current GCC and clang, and
# this makes an older one behave the same. It stays off the MSVC-style driver,
# which has no common-symbol model for it to change.
if(NOT MSVC)
    target_compile_options(wim PRIVATE -fno-common)
endif()

target_link_libraries(wim PRIVATE Threads::Threads)
if(WIN32)
    target_link_libraries(wim PRIVATE ntdll)
endif()

# Install into the bootstrap prefix. Upstream builds with autotools, so its CMake
# — and therefore its install rules — are ours.
#
# All three destinations are needed because a shared library is a different file
# on each platform: Library is the .so/.dylib, runtime is the Windows .dll (which
# belongs beside executables, not in lib/), and ARCHIVE is the .lib import library.
# The versioned names and their symlinks come along with the library rule.
install(TARGETS wim
        LIBRARY DESTINATION lib
        RUNTIME DESTINATION bin
        ARCHIVE DESTINATION lib)

# wimlib.h alone, flat in include/, which is where upstream installs it and
# therefore what `#include <wimlib.h>` means everywhere else. Its private headers
# (include/wimlib/*.h) are not part of the API and are not installed.
install(FILES "${NCOMM_WIMLIB_DIR}/include/wimlib.h" DESTINATION include)

# no licence is installed from here, for the same reason libudfread installs
# none: the licence text still has to travel with the binary, and it travels the
# way every other component's notice does — as files in the application's own
# `licenses/` directory, which the app's build ships whole.
