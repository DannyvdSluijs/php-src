/*
   +----------------------------------------------------------------------+
   | Zend Engine                                                          |
   +----------------------------------------------------------------------+
   | Copyright (c) Zend Technologies Ltd. (http://www.zend.com)           |
   +----------------------------------------------------------------------+
   | This source file is subject to version 2.00 of the Zend license,     |
   | that is bundled with this package in the file LICENSE, and is        |
   | available through the world-wide-web at the following url:           |
   | http://www.zend.com/license/2_00.txt.                                |
   | If you did not receive a copy of the Zend license and are unable to  |
   | obtain it through the world-wide-web, please send a note to          |
   | license@zend.com so we can mail you a copy immediately.              |
   +----------------------------------------------------------------------+
   | Authors: Xinchen Hui <xinchen.h@zend.com>                            |
   +----------------------------------------------------------------------+
*/

#include "zend_cpuinfo.h"

typedef struct _zend_cpu_info {
	uint32_t eax;
	uint32_t ebx;
	uint32_t ecx;
	uint32_t edx;
	uint32_t initialized;
} zend_cpu_info;

static zend_cpu_info cpuinfo = {0};

#if defined(__GNUC__) && (defined(__i386__) || defined(__x86_64__))
# if defined(HAVE_CPUID_H) && defined(HAVE_CPUID_COUNT)
# include <cpuid.h>
static void __zend_cpuid(uint32_t func, uint32_t subfunc, zend_cpu_info *cpuinfo) {
	__cpuid_count(func, subfunc, cpuinfo->eax, cpuinfo->ebx, cpuinfo->ecx, cpuinfo->edx);
}
# else
static void __zend_cpuid(uint32_t func, uint32_t subfunc, zend_cpu_info *cpuinfo) {
#if defined(__i386__) && (defined(__pic__) || defined(__PIC__))
	/* PIC on i386 uses %ebx, so preserve it. */
	__asm__ __volatile__ (
		"pushl  %%ebx\n"
		"cpuid\n"
		"mov    %%ebx,%1\n"
		"popl   %%ebx"
		: "=a"(cpuinfo->eax), "=r"(cpuinfo->ebx), "=c"(cpuinfo->ecx), "=d"(cpuinfo->edx)
		: "a"(func), "c"(subfunc)
	);
#else
	__asm__ __volatile__ (
		"cpuid"
		: "=a"(cpuinfo->eax), "=b"(cpuinfo->ebx), "=c"(cpuinfo->ecx), "=d"(cpuinfo->edx)
		: "a"(func), "c"(subfunc)
	);
#endif
}
# endif
#elif defined(ZEND_WIN32) && !defined(__clang__)
# include <intrin.h>
static void __zend_cpuid(uint32_t func, uint32_t subfunc, zend_cpu_info *cpuinfo) {
	int regs[4];

	__cpuidex(regs, func, subfunc);

	cpuinfo->eax = regs[0];
	cpuinfo->ebx = regs[1];
	cpuinfo->ecx = regs[2];
	cpuinfo->edx = regs[3];
}
#else
static void __zend_cpuid(uint32_t func, uint32_t subfunc, zend_cpu_info *cpuinfo) {
	cpuinfo->eax = 0;
}
#endif

#if defined(__i386__) || defined(__x86_64__)
/* Function based on compiler-rt implementation. */
static unsigned get_xcr0_eax() {
# if defined(__GNUC__) || defined(__clang__)
	// Check xgetbv; this uses a .byte sequence instead of the instruction
	// directly because older assemblers do not include support for xgetbv and
	// there is no easy way to conditionally compile based on the assembler used.
	unsigned eax, edx;
	__asm__(".byte 0x0f, 0x01, 0xd0" : "=a"(eax), "=d"(edx) : "c"(0));
	return eax;
# elif defined(ZEND_WIN32) && defined(_XCR_XFEATURE_ENABLED_MASK)
	return _xgetbv(_XCR_XFEATURE_ENABLED_MASK);
# else
	return 0;
# endif
}

static bool is_avx_supported() {
	if (!(cpuinfo.ecx & ZEND_CPU_FEATURE_AVX)) {
		/* No support for AVX */
		return 0;
	}
	if (!(cpuinfo.ecx & ZEND_CPU_FEATURE_OSXSAVE)) {
		/* The operating system does not support XSAVE. */
		return 0;
	}
	if ((get_xcr0_eax() & 0x6) != 0x6) {
		/* XCR0 SSE and AVX bits must be set. */
		return 0;
	}
	return 1;
}
#else
static bool is_avx_supported(void) {
	return 0;
}
#endif

void zend_cpu_startup(void)
{
	if (!cpuinfo.initialized) {
		zend_cpu_info ebx;
		int max_feature;

		cpuinfo.initialized = 1;
		__zend_cpuid(0, 0, &cpuinfo);
		max_feature = cpuinfo.eax;
		if (max_feature == 0) {
			return;
		}

		__zend_cpuid(1, 0, &cpuinfo);

		/* for avx2 */
		if (max_feature >= 7) {
			__zend_cpuid(7, 0, &ebx);
			cpuinfo.ebx = ebx.ebx;
		} else {
			cpuinfo.ebx = 0;
		}

		if (!is_avx_supported()) {
			cpuinfo.edx &= ~ZEND_CPU_FEATURE_AVX;
			cpuinfo.ebx &= ~(ZEND_CPU_FEATURE_AVX2 & ~ZEND_CPU_EBX_MASK);
		}
	}
}

ZEND_API int zend_cpu_supports(zend_cpu_feature feature) {
	ZEND_ASSERT(cpuinfo.initialized);
	if (feature & ZEND_CPU_EDX_MASK) {
		return (cpuinfo.edx & (feature & ~ZEND_CPU_EDX_MASK));
	} else if (feature & ZEND_CPU_EBX_MASK) {
		return (cpuinfo.ebx & (feature & ~ZEND_CPU_EBX_MASK));
	} else {
		return (cpuinfo.ecx & feature);
	}
}
