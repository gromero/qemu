#include <inttypes.h>

int main(void)
{
    uint8_t *addr = (void *)((1UL << 30) | (1UL << 22)); /* Third 2MB chunk in the second 1GB block. */
    uint64_t temp;

    *addr = 8;

    asm (
          /* Set GCR for randon tag generation. 0xA5 is just a random value to set GCR != 0. */
          "ldr x1, =0xA5;"
          "msr gcr_el1, x1;"

          /* Add tag to pointer 'addr' (logical tag). */
          "mov %[temp], %[addr];"
          "irg %[addr], %[addr];"

          /* Store tag to memory region pointed by 'addr' (allocation tag). */
          "stg %[addr], [%[addr]];"

          /* Test store */
	  "ldr x1, =0xdeadbeef;"
	  "str x1, [x0];"

         : [addr] "+r" (addr)
         : [temp] "r" (temp)
         :
    );
}
