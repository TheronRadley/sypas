/*
 * SYPAS legacy-BIOS diagnostic boot image
 * =========================================
 *
 * ABI / firmware contract:
 *   - This file is assembled as 16-bit code and linked at origin 0x7c00
 *     (the equivalent of `org 0x7c00`).  The El Torito no-emulation entry
 *     loads the resulting 2048-byte image at physical address 0x7c00,
 *     conventionally described as 0000:7c00.  A zero El Torito load-segment
 *     field may be presented by firmware as the equivalent 07c0:0000; both
 *     representations address the same physical bytes.
 *   - CS:IP is supplied by the BIOS/El Torito loader.  DS, ES and SS are
 *     initialized here; no caller stack, disk services, or hardware state
 *     beyond INT 10h AH=0Eh is assumed.
 *   - The only service used is BIOS video teletype: INT 10h, AH=0Eh, with
 *     each message byte in AL.  The BIOS may destroy general registers.
 *   - There is no return ABI.  After the NUL-terminated diagnostic is
 *     printed, interrupts are disabled and the processor is parked in HLT.
 *
 * This is deliberately a diagnostic stub, not a BIOS port of SYPAS.  SYPAS
 * remains UEFI-only and its real boot path is bootloader/uefi/.
 */

.section .text
.code16
.globl start
.type start, @function

start:
    cli
    xorw    %ax, %ax
    movw    %ax, %ds
    movw    %ax, %es
    movw    %ax, %ss
    movw    $0x7000, %sp

    movw    $message, %si

.Lprint:
    lodsb
    testb   %al, %al
    jz      .Lhalt
    movb    $0x0e, %ah
    xorb    %bh, %bh             /* display page zero */
    pushw   %si                  /* do not depend on BIOS preserving SI */
    int     $0x10
    popw    %si
    jmp     .Lprint

.Lhalt:
    cli
.Lhalt_loop:
    hlt
    jmp     .Lhalt_loop

message:
    .ascii  "SYPAS 0.1.0 - This machine booted in legacy BIOS mode, but SYPAS requires UEFI firmware (x86_64).\r\n"
    .ascii  "VirtualBox: Settings > System > Motherboard > check 'Enable EFI' and set Base Memory to 2048 MB, then restart the VM.\r\n"
    .ascii  "VMware: Firmware type UEFI. QEMU: boot with OVMF. System halted.\r\n"
    .byte   0

/* The no-emulation image is exactly four 512-byte virtual sectors. */
.org 2048, 0x00

.size start, . - start
