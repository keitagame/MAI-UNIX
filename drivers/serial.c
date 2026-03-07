#define COM1 0x3F8
static inline void outb(uint16_t port, uint8_t val) {
    asm volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    asm volatile("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}
int serial_read_char(void) {
    while ((inb(COM1 + 5) & 1) == 0);
    return inb(COM1);
}
