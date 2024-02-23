/*
	TITLE: CHR sprite extractor v0.1
	By Jason Betts	

	It takes 32 bytes to make one sprite. Each row requires 2 bytes due to each
	pixel needing 2 bits to represent a value of 0-3.
	We need to combine two bytes to get the value of each pixel.

	We need to compare byte 0 with byte 1, then byte 2 with byte 3 etc.

	The two bytes are added together to make an * pixel row as below:
	  0  1  0  0  1  1  0  1
	+ 1  1  0  0  0  1  0  0
	------------------------
	  2  3  0  0  1  3  0  1

	  TODO:	- Detect if there is CHR-RAM and rekect the rom.					[ ]
			- Fix if there is more than 1 CHR bank the program will segfault	[ ]
			- Add color palettes												[ ]
			- Outfile name in argv[2]											[ ]
			- Extract to a PNG with alpha, will need a library for this			[ ]
*/

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

typedef uint8_t   ubyte;
typedef uint8_t   BYTE;
typedef uint32_t  DWORD;
typedef int32_t   LONG;
typedef uint16_t  WORD;

#pragma pack(push, 1)
typedef struct
{
	WORD   bfType;
	DWORD  bfSize;
	WORD   bfReserved1;
	WORD   bfReserved2;
	DWORD  bfOffBits;
}
bmpfileheader;

#define BITMAP_FILEHEADER_SIZE 14

typedef struct BITMAPINFOHEADER {
	DWORD biSize;
	LONG  biWidth;
	LONG  biHeight;
	WORD  biPlanes;
	WORD  biBitCount;
	DWORD biCompression;
	DWORD biSizeImage;
	LONG  biXPelsPerMeter;
	LONG  biYPelsPerMeter;
	DWORD biClrUsed;
	DWORD biClrImportant;
}
bmpinfoheader;

typedef struct
{
	BYTE blue;
	BYTE green;
	BYTE red;
	BYTE alpha;
} RGB;

typedef struct _inesheader
{
	ubyte check[4];				// should be constant 0x4E 0x45 0x53 0x1A followed by MS-DOS end of file
	ubyte prg_rom_chunks;		// Size of PRG ROM is 16KB units
	ubyte chr_rom_chunks;		// Size of CHR ROM in 8 KB units (0 means the board uses CHR RAM)
	ubyte mapper1;				// Mapper, mirroring, battery, trainer
	ubyte mapper2;				// Mapper, VS/Playchoice, NES2.0
	ubyte prg_ram_size;			// PRG-RAM size (rarely used extension)
	ubyte tv_system1;			// TV system (rarely used extension)
	ubyte tv_system2;			// TV system, PRG-RAM presence (unofficial, rarely used extension)
	ubyte padding[5];			// Unused padding (should be filled with zero, but some rippers put thier name across bytes 7 -15)

} iNESHEADER;

typedef struct _inesrom
{
	iNESHEADER header;
	ubyte mapper_id;
	ubyte* PRG_data;
	ubyte* CHR_data;
} iNESROM;
#pragma pack(pop)

// Grey scale pixels for sprites. TODO: Make some palettes
int translate_color(int pixel)
{
	if (pixel == 1) {
		return 0xAAAAAA; // light grey
	}

	if (pixel == 2) {
		return 0x464646; // dark grey
	}

	if (pixel == 3) {
		return 0x0; // black
	}

	return 0xFF00FF; // magenta for alpha of 0
}

bool extract_chr(iNESROM* rom)
{
	ubyte* bit = rom->CHR_data;
	FILE* fp;
	bmpfileheader bf;
	bmpinfoheader bi;

	// get the amount of sprites in the CHR rom. 32 bytes per sprite
	int sprite_num = (rom->header.chr_rom_chunks * 8192) / 32;
	int width = sprite_num / 2;
	int height = width;

	// Set-up bitmap headers
	bf.bfOffBits = 54;
	bf.bfReserved1 = 0;
	bf.bfReserved2 = 0;
	bf.bfType = 0x4d42;
	bf.bfSize = 54 + (width * height * sizeof(int));

	bi.biBitCount = 32;
	bi.biCompression = 0;
	bi.biHeight = height;
	bi.biWidth = width;
	bi.biSizeImage = 0;
	bi.biSize = sizeof(bi);
	bi.biPlanes = 1;
	bi.biXPelsPerMeter = 0;
	bi.biYPelsPerMeter = 0;
	bi.biClrUsed = 0;

	// Need VLA support to compile this
	int(*sprites)[height] = calloc(height, width  * sizeof(int));

	int num_tiles = height / 8;
	int bytes_per_row = num_tiles * 16;

	// Loop through the entire CHR memory and extract each sprite 1 at a time
	for (int tileY = 0; tileY < num_tiles; ++tileY) {

		// This is reverse order on the x axis to get the correct orientation of the sprite sheet in the output image
		for (int tileX = num_tiles - 1; tileX >= 0; --tileX) {

			int offset = tileY * bytes_per_row + tileX * num_tiles;

			for (int row = 0; row < 8; ++row) {

				// get the bytes we need to add together to get the pixel value
				ubyte tile_lsb = *(bit + offset + row + 0);
				ubyte tile_msb = *(bit + offset + row + 8);

				int y = height - (tileY * 8) - row - 1; // BMP is bottom up, so write Y axis to sprites buffer in reverse

				// We need to write the columns in reverse order to get them in the correct direction for the outfile
				for (int col = 7; col >= 0; --col) {					

					// Get the sprites pixel value by binary adding the bits
					ubyte pixel = ((tile_lsb & 0x01) && (tile_msb & 0x01)) ? 3 :
						((tile_lsb & 0x01) && (tile_msb | 0x01)) ? 1 :
						((tile_lsb | 0x01) && (tile_msb & 0x01)) ? 2 : 0;

					// Write the sprite data to the pixel memory after translating the binary addidtion result to a palette colour
					sprites[y][tileX * 8 + col] = translate_color(pixel);

					// Right shift so we can add the next bits
					tile_lsb >>= 1;
					tile_msb >>= 1;					
				}
			}
		}
	}
	printf("Extracted %d x %d sprite sheet\n", width / 8, height / 8);

	fp = fopen("test2.bmp", "wb");

	if (!fp) {
		perror("Error");
		return false;
	}

	fwrite(&bf, sizeof(bf), 1, fp);
	fwrite(&bi, sizeof(bi), 1, fp);
	fwrite(sprites, height * width * sizeof(int), 1, fp);

	if(fp)
		fclose(fp);

	if(sprites)
		free(sprites);	

	return true;
}

iNESROM* read_rom(const char* rom_name)
{
	iNESROM* rom;
	FILE* fp;
	ubyte PRGBanks, CHRBanks;

	fp = fopen(rom_name, "rb");

	if (!fp) {
		perror("fopen");
		return NULL;
	}

	rom = malloc(sizeof(iNESROM));

	if (rom == 0) {
		perror("malloc");
		return NULL;
	}
	fread(&rom->header, 1, sizeof(iNESHEADER), fp);

	// Check the header is a NES file
	int *check_header = (int *)&rom->header;
	if (*check_header != 0x1A53454E) // = NES0x4e
		return NULL;

	if (rom->header.mapper1 & 0x04)
		fseek(fp, 512, SEEK_CUR);

	// Determine mapper ID
	rom->mapper_id = ((rom->header.mapper2 >> 4) << 4) | (rom->header.mapper1 >> 4);

	printf("CHR has %d banks\n", rom->header.chr_rom_chunks);

	PRGBanks = rom->header.prg_rom_chunks;
	rom->PRG_data = (ubyte *)malloc(PRGBanks * 16384);

	if (rom->PRG_data == 0)
		return NULL;

	fread(rom->PRG_data, 1, PRGBanks * 16384, fp);

	CHRBanks = rom->header.chr_rom_chunks;
	rom->CHR_data = malloc(CHRBanks * 8192);

	if (rom->CHR_data == 0)
		return NULL;

	fread(rom->CHR_data, 1, CHRBanks * 8192, fp);
	
	if(fp)
		fclose(fp);

	return (rom) ? rom : NULL;
}

int main(int argc, char* argv[])
{
	iNESROM* rom = read_rom((argv[1]) ? argv[1] : "zelda.nes");

	if (!rom) {		
		return 1;
	}

	if (!extract_chr(rom))
		return 1;

	return 0;
}