#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <sys/utsname.h>
#include "builtins.h"
#include "ui.h"
#include "syscall.h"

/*
 * Built-in: lscpu
 * Displays x86-64 CPU & virtual hardware details queried directly via CPUID
 * inside Ring 3 plus MiniKernel ABI telemetry (without any host OS /proc dependency).
 */
static void cpuid_leaf(uint32_t leaf, uint32_t *eax, uint32_t *ebx, uint32_t *ecx, uint32_t *edx)
{
    __asm__ volatile("cpuid"
                     : "=a"(*eax), "=b"(*ebx), "=c"(*ecx), "=d"(*edx)
                     : "a"(leaf), "c"(0));
}

int builtin_lscpu(int argc, char **argv, ShellState *state)
{
    (void)argc;
    (void)argv;
    (void)state;

    struct utsname un;
    memset(&un, 0, sizeof(un));
    uname(&un);

    uint32_t max_leaf = 0, ebx = 0, ecx = 0, edx = 0;
    cpuid_leaf(0, &max_leaf, &ebx, &ecx, &edx);

    char vendor[13];
    memcpy(vendor + 0, &ebx, 4);
    memcpy(vendor + 4, &edx, 4);
    memcpy(vendor + 8, &ecx, 4);
    vendor[12] = '\0';

    char brand[49];
    memset(brand, 0, sizeof(brand));
    uint32_t ext_max = 0, dummy1 = 0, dummy2 = 0, dummy3 = 0;
    cpuid_leaf(0x80000000U, &ext_max, &dummy1, &dummy2, &dummy3);
    if (ext_max >= 0x80000004U) {
        uint32_t *b = (uint32_t *)brand;
        cpuid_leaf(0x80000002U, &b[0], &b[1], &b[2], &b[3]);
        cpuid_leaf(0x80000003U, &b[4], &b[5], &b[6], &b[7]);
        cpuid_leaf(0x80000004U, &b[8], &b[9], &b[10], &b[11]);
        brand[48] = '\0';
    } else {
        strncpy(brand, "x86-64 MiniKernel CPU", sizeof(brand) - 1);
    }

    uint32_t eax1 = 0, ebx1 = 0, ecx1 = 0, edx1 = 0;
    cpuid_leaf(1, &eax1, &ebx1, &ecx1, &edx1);
    uint32_t stepping = eax1 & 0xF;
    uint32_t model    = ((eax1 >> 4) & 0xF) | (((eax1 >> 16) & 0xF) << 4);
    uint32_t family   = ((eax1 >> 8) & 0xF) + ((eax1 >> 20) & 0xFF);
    bool has_vmx      = (ecx1 & (1U << 5)) != 0;
    bool has_hyperv   = (ecx1 & (1U << 31)) != 0;

    uint16_t cs = 0;
    __asm__ volatile("mov %%cs, %0" : "=r"(cs));

    printf("\n%sArchitecture:%s            %s\n", COLOR_BOLD, COLOR_RESET, un.machine[0] ? un.machine : "x86_64");
    printf("  %sCPU op-mode(s):%s        32-bit, 64-bit Long Mode\n", COLOR_DIM, COLOR_RESET);
    printf("  %sByte Order:%s            Little Endian\n", COLOR_DIM, COLOR_RESET);
    printf("  %sPrivilege Level:%s       Ring %d (CS Selector 0x%04x)\n", COLOR_DIM, COLOR_RESET, cs & 3, cs);
    printf("%sCPU(s):%s                  1 (PIT 100 Hz Preemptive)\n", COLOR_BOLD, COLOR_RESET);
    printf("%sVendor ID:%s               %s\n", COLOR_BOLD, COLOR_RESET, vendor);
    printf("  %sModel name:%s            %s\n", COLOR_BOLD, COLOR_RESET, brand[0] ? brand : "x86-64 Virtual CPU");
    printf("    %sCPU family:%s          %u\n", COLOR_DIM, COLOR_RESET, family);
    printf("    %sModel:%s               %u\n", COLOR_DIM, COLOR_RESET, model);
    printf("    %sStepping:%s            %u\n", COLOR_DIM, COLOR_RESET, stepping);
    printf("%sVirtualization features:%s\n", COLOR_BOLD, COLOR_RESET);
    printf("  %sHypervisor:%s            %s\n", COLOR_DIM, COLOR_RESET,
           has_hyperv ? "QEMU / Virtualized Silicon" : (has_vmx ? "VT-x Capable" : "Bare-Metal x86-64"));
    printf("\n%s[Kernel Verified]%s Executed direct CPUID hardware instruction + MiniKernel uname ABI\n\n",
           COLOR_GREEN, COLOR_RESET);

    return 0;
}
