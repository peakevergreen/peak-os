#include "pci.h"
#include "util.h"

#define PCI_CONFIG_ADDR 0xCF8
#define PCI_CONFIG_DATA 0xCFC

static void outl_port(uint16_t port, uint32_t val) {
    __asm__ volatile("outl %0, %w1" : : "a"(val), "Nd"(port));
}

static uint32_t inl_port(uint16_t port) {
    uint32_t val;
    __asm__ volatile("inl %w1, %0" : "=a"(val) : "Nd"(port));
    return val;
}

static uint32_t make_addr(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    return (uint32_t)(1u << 31) |
           ((uint32_t)bus << 16) |
           ((uint32_t)slot << 11) |
           ((uint32_t)func << 8) |
           ((uint32_t)offset & 0xFCu);
}

uint32_t pci_read32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    outl_port(PCI_CONFIG_ADDR, make_addr(bus, slot, func, offset));
    return inl_port(PCI_CONFIG_DATA);
}

void pci_write32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t val) {
    outl_port(PCI_CONFIG_ADDR, make_addr(bus, slot, func, offset));
    outl_port(PCI_CONFIG_DATA, val);
}

uint16_t pci_read16(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    uint32_t v = pci_read32(bus, slot, func, offset & 0xFC);
    return (uint16_t)((v >> ((offset & 2) * 8)) & 0xFFFF);
}

uint8_t pci_read8(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    uint32_t v = pci_read32(bus, slot, func, offset & 0xFC);
    return (uint8_t)((v >> ((offset & 3) * 8)) & 0xFF);
}

void pci_write16(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint16_t val) {
    uint32_t o = offset & 0xFC;
    uint32_t cur = pci_read32(bus, slot, func, o);
    uint32_t shift = (offset & 2) * 8;
    cur = (cur & ~(0xFFFFu << shift)) | ((uint32_t)val << shift);
    pci_write32(bus, slot, func, o, cur);
}

void pci_enable_bus_master(struct pci_device *dev) {
    uint16_t cmd = pci_read16(dev->bus, dev->slot, dev->func, 0x04);
    cmd |= 0x07;
    pci_write16(dev->bus, dev->slot, dev->func, 0x04, cmd);
}

int pci_find(uint16_t vendor, uint16_t device, struct pci_device *out) {
    for (int bus = 0; bus < 256; bus++) {
        for (int slot = 0; slot < 32; slot++) {
            for (int func = 0; func < 8; func++) {
                uint16_t v = pci_read16((uint8_t)bus, (uint8_t)slot, (uint8_t)func, 0);
                if (v == 0xFFFF)
                    continue;
                uint16_t d = pci_read16((uint8_t)bus, (uint8_t)slot, (uint8_t)func, 2);
                if (v != vendor || d != device)
                    continue;
                out->bus = (uint8_t)bus;
                out->slot = (uint8_t)slot;
                out->func = (uint8_t)func;
                out->vendor = v;
                out->device = d;
                uint32_t classreg = pci_read32((uint8_t)bus, (uint8_t)slot, (uint8_t)func, 0x08);
                out->class_code = (uint8_t)((classreg >> 24) & 0xFF);
                out->subclass = (uint8_t)((classreg >> 16) & 0xFF);
                out->prog_if = (uint8_t)((classreg >> 8) & 0xFF);
                out->irq_line = pci_read8((uint8_t)bus, (uint8_t)slot, (uint8_t)func, 0x3C);
                out->bar0 = pci_read32((uint8_t)bus, (uint8_t)slot, (uint8_t)func, 0x10);
                out->bar1 = pci_read32((uint8_t)bus, (uint8_t)slot, (uint8_t)func, 0x14);
                return 0;
            }
        }
    }
    return -1;
}
