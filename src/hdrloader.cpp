
/***********************************************************************************
	Created:	17:9:2002
	FileName: 	hdrloader.cpp
	Author:		Igor Kravtchenko
	
	Info:		Load HDR image and convert to a set of float32 RGB triplet.
************************************************************************************/

#include "hdrloader.h"
#include <QString>

#include <math.h>
#include <memory.h>
#include <stdio.h>

typedef unsigned char RGBE[4];
#define R			0
#define G			1
#define B			2
#define E			3

#define  MINELEN	8				// minimum scanline length for encoding
#define  MAXELEN	0x7fff			// maximum scanline length for encoding

static void workOnRGBE(RGBE *scan, int len, float *cols);
static bool decrunch(RGBE *scanline, int len, FILE *file);
static bool oldDecrunch(RGBE *scanline, int len, FILE *file);

bool HDRLoader::load(const char *fileName, HDRLoaderResult &res)
{
	int i;
	char str[200];
	FILE *file;

	errno_t err;
	err=_wfopen_s(&file, reinterpret_cast<const wchar_t*>(QString::fromUtf8(fileName).utf16()), L"rb");
	if (err!=0)
		return false;

	if (fread(str, 10, 1, file) != 1 || memcmp(str, "#?RADIANCE", 10)) {
		fclose(file);
		return false;
	}

	fseek(file, 1, SEEK_CUR);

	char cmd[200];
	i = 0;
	int c = 0, oldc;
	while(true) {
		oldc = c;
		c = fgetc(file);
		if (c == EOF || i >= 199) { fclose(file); return false; }
		if (c == 0xa && oldc == 0xa)
			break;
		cmd[i++] = c;
	}

	char reso[200];
	i = 0;
	while(true) {
		c = fgetc(file);
		if (c == EOF || i >= 199) { fclose(file); return false; }
		reso[i++] = c;
		if (c == 0xa)
			break;
	}
	reso[i] = 0;

	int w, h;
	if (sscanf_s(reso, "-Y %d +X %d", &h, &w) != 2 || w <= 0 || h <= 0 || w > 32767 || h > 32767 || static_cast<long long>(w)*h > 134217728) {
		fclose(file);
		return false;
	}

	res.width = w;
	res.height = h;

	float *cols = new float[w * h * 3];
	res.cols = cols;

	RGBE *scanline = new RGBE[w];
	if (!scanline) {
		fclose(file);
		return false;
	}

	// convert image 
	for (int y = h - 1; y >= 0; y--) {
		if (decrunch(scanline, w, file) == false) {
			delete[] scanline; delete[] res.cols; res.cols=nullptr; res.width=res.height=0;
			fclose(file); return false;
		}
		workOnRGBE(scanline, w, cols);
		cols += w * 3;
	}

	delete [] scanline;
	fclose(file);

	return true;
}

float convertComponent(int expo, int val)
{
	float v = val / 256.0f;
	float d = (float) pow(2, expo);
	return v * d;
}

void workOnRGBE(RGBE *scan, int len, float *cols)
{
	while (len-- > 0) {
		int expo = scan[0][E] - 128;
		cols[0] = convertComponent(expo, scan[0][R]);
		cols[1] = convertComponent(expo, scan[0][G]);
		cols[2] = convertComponent(expo, scan[0][B]);
		cols += 3;
		scan++;
	}
}

bool decrunch(RGBE *scanline, int len, FILE *file)
{
	int  i, j;
					
	if (len < MINELEN || len > MAXELEN)
		return oldDecrunch(scanline, len, file);

	i = fgetc(file);
	if (i == EOF) return false;
	if (i != 2) {
		fseek(file, -1, SEEK_CUR);
		return oldDecrunch(scanline, len, file);
	}

	scanline[0][G] = fgetc(file);
	scanline[0][B] = fgetc(file);
	i = fgetc(file);

	if (scanline[0][G] != 2 || scanline[0][B] & 128) {
		scanline[0][R] = 2;
		scanline[0][E] = i;
		return oldDecrunch(scanline + 1, len - 1, file);
	}
	if (i == EOF || ((int(scanline[0][B]) << 8) | i) != len) return false;

	// read each component
	for (i = 0; i < 4; i++) {
	    for (j = 0; j < len; ) {
			int code = fgetc(file);
			if (code <= 0 || (code > 128 ? code - 128 : code) > len-j) return false;
			if (code > 128) { // run
			    code &= 127;
			    int val = fgetc(file);
			    if (val == EOF) return false;
			    while (code--)
					scanline[j++][i] = val;
			}
			else  {	// non-run
			    while(code--) {
					const int value=fgetc(file); if(value==EOF) return false;
					scanline[j++][i] = value;
				}
			}
		}
    }

	return feof(file) ? false : true;
}

bool oldDecrunch(RGBE *scanline, int len, FILE *file)
{
	int i;
	int rshift = 0;
	const RGBE* start = scanline;
	
	while (len > 0) {
		scanline[0][R] = fgetc(file);
		scanline[0][G] = fgetc(file);
		scanline[0][B] = fgetc(file);
		scanline[0][E] = fgetc(file);
		if (feof(file))
			return false;

		if (scanline[0][R] == 1 &&
			scanline[0][G] == 1 &&
			scanline[0][B] == 1) {
			if(scanline==start || rshift>24 || (static_cast<unsigned long long>(scanline[0][E]) << rshift)>static_cast<unsigned>(len)) return false;
			for (i = scanline[0][E] << rshift; i > 0; i--) {
				memcpy(&scanline[0][0], &scanline[-1][0], 4);
				scanline++;
				len--;
			}
			rshift += 8;
		}
		else {
			scanline++;
			len--;
			rshift = 0;
		}
	}
	return true;
}
