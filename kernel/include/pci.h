#ifndef PEAK_PCI_H
#define PEAK_PCI_H

#include "types.h"

struct pci_device {
    uint8_t bus, slot, func;
    uint16_t vendor;
    uint16_t device;
    uint8_t class_code;
    uint8_t subclass;
    uint8_t prog_if;
    uint8_t irq_line;
    uint32_t bar0;
    uint32_t bar1;
};

uint32_t pci_read32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset);
uint16_t pci_read16(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset);
uint8_t  pci_read8(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset);
void     pci_write32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t val);
void     pci_write16(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint16_t val);

/* Find first matching vendor:device. Returns 0 on success. */
int pci_find(uint16_t vendor, uint16_t device, struct pci_device *out);
void pci_enable_bus_master(struct pci_device *dev);

#endif
