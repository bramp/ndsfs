#include <stdint.h>

#pragma pack(1)

struct NDSHeader {
	int8_t title[0xC];
	int8_t gamecode[0x4];
	int8_t makercode[2];
	uint8_t unitcode;							// product code. 0 = Nintendo DS
	uint8_t devicetype;						// device code. 0 = normal
	uint8_t devicecap;						// device size. (1<<n Mbit)
	uint8_t reserved1[0x9];					// 0x015..0x01D
	uint8_t romversion;
	uint8_t reserved2;						// 0x01F
	uint32_t arm9_rom_offset;					// points to libsyscall and rest of ARM9 binary
	uint32_t arm9_entry_address;
	uint32_t arm9_ram_address;
	uint32_t arm9_size;
	uint32_t arm7_rom_offset;
	uint32_t arm7_entry_address;
	uint32_t arm7_ram_address;
	uint32_t arm7_size;
	uint32_t fnt_offset;
	uint32_t fnt_size;
	uint32_t fat_offset;
	uint32_t fat_size;
	uint32_t arm9_overlay_offset;
	uint32_t arm9_overlay_size;
	uint32_t arm7_overlay_offset;
	uint32_t arm7_overlay_size;
	uint32_t rom_control_info1;					// 0x00416657 for OneTimePROM
	uint32_t rom_control_info2;					// 0x081808F8 for OneTimePROM
	uint32_t banner_offset;
	uint16_t secure_area_crc;
	uint16_t rom_control_info3;				// 0x0D7E for OneTimePROM
	uint32_t offset_0x70;						// magic1 (64 bit encrypted magic code to disable LFSR)
	uint32_t offset_0x74;						// magic2
	uint32_t offset_0x78;						// unique ID for homebrew
	uint32_t offset_0x7C;						// unique ID for homebrew
	uint32_t application_end_offset;			// rom size
	uint32_t rom_header_size;
	uint32_t offset_0x88;						// reserved... ?
	uint32_t offset_0x8C;

	// reserved
	uint32_t offset_0x90;
	uint32_t offset_0x94;
	uint32_t offset_0x98;
	uint32_t offset_0x9C;
	uint32_t offset_0xA0;
	uint32_t offset_0xA4;
	uint32_t offset_0xA8;
	uint32_t offset_0xAC;
	uint32_t offset_0xB0;
	uint32_t offset_0xB4;
	uint32_t offset_0xB8;
	uint32_t offset_0xBC;

	uint8_t logo[156];						// character data
	uint16_t logo_crc;
	uint16_t header_crc;

	// 0x160..0x17F reserved
	uint32_t offset_0x160;
	uint32_t offset_0x164;
	uint32_t offset_0x168;
	uint32_t offset_0x16C;
	uint8_t zero[0x90];
};

#pragma pack()
