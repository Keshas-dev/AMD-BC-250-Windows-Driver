/*
 * msvc_compat.h - MSVC compatibility shims for the BC-250 EFI unlock sources.
 *
 * Upstream (Hexxeh/bc250-efi-core-unlock) builds with clang or mingw-w64 and
 * reaches for GNU inline assembly in three places. MSVC x64 forbids inline asm
 * entirely, so these map onto the real MSVC intrinsics instead:
 *
 *   outpd(port, val)  ->  _outpd()   <intrin.h>
 *   inpd(port)        ->  _inpd()    <intrin.h>
 *   __asm__("hlt")    ->  __halt()   <intrin.h>
 *
 * The x64 spellings are the single-underscore pair. Verified on MSVC 14.44 by
 * compiling probes: _outpd/_inpd/__halt all resolve, while the double-underscore
 * __outpd/__inpd and the x86-only __inp/__inp* are not declared for x64 and
 * fail with C4013 followed by LNK2019.
 *
 * outpd and inpd are *already* MSVC intrinsic names, so they cannot be wrapped
 * in functions - C2169 rejects a definition. They are therefore macros, which
 * is why the ported .c files compile unmodified and stay diffable against
 * upstream.
 *
 * The three intrinsics are declared by hand rather than by including
 * <intrin.h>. intrin.h pulls in xmmintrin.h, which includes malloc.h, which
 * lives in the Windows Kits UCRT headers - so including it would make this build
 * depend on the Windows Kits being installed, for three one-line declarations
 * that need no headers at all. Declared this way it compiles and links with
 * nothing but the MSVC compiler, which matters because the Kits live on a drive
 * that is not always mounted. Verified: a probe using only these declarations
 * compiles clean and links to a 1536-byte EFI application.
 *
 * Everything else in those files is plain C99.
 */
#ifndef MSVC_COMPAT_H
#define MSVC_COMPAT_H

#include <stdint.h>

/* Hand-declared MSVC x64 intrinsics. */
unsigned long __cdecl _outpd(unsigned short port, unsigned long value);
unsigned long __cdecl _inpd(unsigned short port);
void          __cdecl __halt(void);

#ifndef NULL
#define NULL ((void *)0)
#endif

/* Same names/signatures as upstream so the ported sources need no changes. */
#define outpd(port, val)  _outpd((unsigned short)(port), (unsigned long)(val))
#define inpd(port)        ((unsigned int)_inpd((unsigned short)(port)))

/* Upstream casts a UINTN physical address straight to a pointer, which MSVC
 * warns about with C4312. UINT64 is the same width but is understood as a
 * pointer-sized value, so route through it.
 *
 * The type argument already includes the '*', so the macro must not add one -
 * doing so yields a pointer-to-pointer (C4047). */
#define PHYS_TO_PTR(type, addr) ((type)(UINT64)(addr))

static __inline void halt_cpu(void)
{
    __halt();
}

#endif /* MSVC_COMPAT_H */
