#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#define u8 unsigned char
#define u16 unsigned short
#define u32 unsigned int

typedef enum {
	SQ_START = 0,
	SQ_BG,	//BG
	SQ_END
} SQ_TYPE;

typedef struct tagBITMAPFILEHEADER {
	u16 bfType;
	u32 bfSize;
	u16 bfReserved1;
	u16 bfReserved2;
	u32 bfOffBits;
} BITMAPFILEHEADER;

typedef enum {
	BI_RGB = 0,
	BI_RLE8,
	BI_BLE4,
	BI_BITFILEDS,
	BI_JPEG,
	BI_PNG,
} BI_COMPRESSION;

typedef struct tagBITMAPINFOHEADER {
	u32 biSize;
	u32 biWidth;
	u32 biHeight;
	u16 biPlanes;
	u16 biBitCount;
	u32 biCompression;
	u32 biSizeImage;
	u32 biXPelsPerMeter;
	u32 biYPelsPerMeter;
	u32 biClrUsed;
	u32 biClrImportant;
} BITMAPINFOHEADER;

typedef struct tagRGBQUAD {
	u8 rgbBlue;
	u8 rgbGreen;
	u8 rgbRed;
	u8 alpha;
} RGBQUAD;

static void parse_args(int argc, char **argv, int *width, int *height, int *raw_bits_per_pixl, int *sq_type, char **raw_file)
{
	int ch;
	opterr = 0;

	while ((ch = getopt(argc, argv, "w:h:b:f:s:")) != -1){
		printf("optind:%d\n", optind);
		printf("optarg:%s\n", optarg);
		printf("ch:%c\n", ch);
		switch (ch) {
		case 'w':
			*width = atoi(optarg);
			break;

		case 'h':
			*height = atoi(optarg);
			break;

		case 'b':
			*raw_bits_per_pixl = atoi(optarg);
			break;

		case 'f':
			*raw_file = (char *)malloc(strlen((const char *)optarg) + 1);
			if (!*raw_file) {
				printf("malloc file name for raw_file failed\n");
				break;
			}
			strcpy(*raw_file, optarg);
			break;

		case 's':
			*sq_type = atoi(optarg);
			break;

		default:
			printf("other option:%c\n", ch);
			break;
		}
		printf("optopt:%c\n", optopt);
	}
}

static void raw8bits_to_rgb24bits(unsigned char *raw, unsigned char *rgb, int width, int height)
{
	int line;
	int offset = 0;

	for (line = 1; line <= height; line++) {
		int row;
		if (line % 2) {
			//BG
			for (row = 1; row <= width; row++) {
				if (row % 2) {
					//B
					rgb[offset] = raw[width * line + row];
					rgb[offset + 1] = raw[width * (line - 1) + row];
					rgb[offset + 2] = raw[width * (line - 1) + row - 1];
					offset += 3;
				} else {
					//G
					rgb[offset] = raw[width * line + row - 1];
					rgb[offset + 1] = (raw[width * (line - 1) + row - 1] + raw[width * line + row - 2]) / 2;
					rgb[offset + 2] = raw[width * (line - 1) + row - 2];
					offset += 3;
				}
			}
		} else {
			//GR
			for (row = 1; row <= width; row++) {
				if (row % 2) {
					//G
					rgb[offset] = raw[width * (line - 1) + row];
					rgb[offset + 1] = raw[width * (line - 1) + row - 1];
					rgb[offset + 2] = raw[width * (line - 2) + row - 1];
					offset += 3;
				} else {
					//R
					rgb[offset] = raw[width * (line - 1) + row - 1];
					rgb[offset + 1] = raw[width * (line - 1) + row - 2];
					rgb[offset + 2] = raw[width * (line - 2) + row - 2];
					offset += 3;
				}
			}
		}
	}
}

/*
 * -w raw宽度
 * -h raw高度
 * -b raw书籍pixel位宽
 * -f raw文件名
 * -s raw数据起始分量，参看SQ_TYPE
 */
int main(int argc, char **argv)
{
	int ret = 0;
	int width, height;
	int raw_bits_per_pixl, bmp_bits_per_pixl = 8;
	int sq_type = SQ_END;
	int raw_size, bmp_data_size, rgb_data_size;
	char *raw_file_name = NULL;
	char *bmp_file_name = NULL;
	unsigned char *raw;
	unsigned char *bmp_data;
	unsigned char *rgb_data;
	int raw_file;
	int bmp_file;
	BITMAPFILEHEADER bf;
	BITMAPINFOHEADER bi;
	RGBQUAD rgb;

	parse_args(argc, argv, &width, &height, &raw_bits_per_pixl, &sq_type, &raw_file_name);
	if ((sq_type <= SQ_START) || (sq_type >= SQ_END)) {
		printf("Don't support sq_type:%d\n", sq_type);
		return -1;
	}
	printf("width:%d, height:%d, raw_bits_per_pixl:%d\n", width, height, raw_bits_per_pixl);
	if (raw_file_name) {
		int first_name_len;
		char *p = strrchr(raw_file_name, '.');

		if (p) {
			first_name_len = p - raw_file_name;
		} else {
			first_name_len = strlen(raw_file_name);
		}

		bmp_file_name = (char *)malloc(first_name_len + 5);
		if (bmp_file_name) {
			strncpy(bmp_file_name, raw_file_name, first_name_len);
			*(bmp_file_name + first_name_len) = '\0';
			strcat(bmp_file_name, ".bmp");
		} else {
			printf("malloc for bmp file name failed\n");
		}
	}

	raw_size = width * height * raw_bits_per_pixl / 8;
	raw = (unsigned char *)malloc(raw_size);
	if (!raw) {
		printf("malloc for raw data failed\n");
		ret = -1;
		goto end;
	}
	memset(raw, 0, raw_size);

	bmp_data_size = width * height;
	bmp_data = (unsigned char *)malloc(bmp_data_size);
	if (!bmp_data) {
		printf("malloc for bmp data failed\n");
		ret = -1;
		goto free_raw;
	}
	memset(bmp_data, 0, bmp_data_size);

	rgb_data_size = bmp_data_size * 3;
	rgb_data = (unsigned char *)malloc(rgb_data_size);
	if (!rgb_data) {
		printf("malloc for rgb data failed\n");
		ret = -1;
		goto free_bmp;
	}
	memset(rgb_data, 0, rgb_data_size);

	raw_file = open(raw_file_name, O_RDONLY);
	if (raw_file == -1) {
		printf("open raw file failed\n");
		ret = -1;
		goto free_rgb_data;
	}
	read(raw_file, raw, raw_size);

	bmp_file = open(bmp_file_name, O_RDWR | O_CREAT | O_TRUNC, S_IRWXU/* | S_IRWXG | S_IRWXO*/);
	if (bmp_file == -1) {
		printf("open bmp file failed\n");
		ret = -1;
		goto close_raw_file;
	}

	if (raw_bits_per_pixl != bmp_bits_per_pixl) {
		/* 处理为8bits位宽，即使 10/12bits --> 8bits */
		int i;
		int pixels = width * height;
		for (i = 0; i < pixels; i++) {
			int off_byte = raw_bits_per_pixl * i / 8;
			int off_bits = raw_bits_per_pixl * i % 8;
			int value = (*(raw + off_byte)) >> off_bits;

			u8 next_byte_value = *(raw + off_byte + 1);
			int next_byte_off_bits = raw_bits_per_pixl - (8 - off_bits);
			u8 tmp = next_byte_value << (8 - next_byte_off_bits);
			tmp >>= (8 - next_byte_off_bits);
			value += tmp << (8 - off_bits);

			*(bmp_data + i) = value * (1 << bmp_bits_per_pixl) / (1 << raw_bits_per_pixl);
		}
	} else {
		int i;
		int pixels = width * height;
		for (i = 0; i < pixels; i++) {
			*(bmp_data + i) = *(raw + i);
		}
	}

	//位图头文件（包含有关文件类型，大小，存放位置等信息）
	bf.bfType = ((u16)('M' << 8) | 'B');	//"BM"说明文件类型
	bf.bfReserved1 = 0;
	bf.bfReserved2 = 0;
	//bf.bfSize = sizeof(BITMAPFILEHEADER) - 2 + sizeof(BITMAPINFOHEADER) + sizeof(RGBQUAD) * 256 + bmp_data_size;//文件大小
	bf.bfSize = sizeof(BITMAPFILEHEADER) - 2 + sizeof(BITMAPINFOHEADER) + rgb_data_size;//文件大小
	bf.bfOffBits = sizeof(BITMAPFILEHEADER) - 2 + sizeof(BITMAPINFOHEADER);//表示从头文件开始到实际图像数据之间的字节的偏移，bfOffBits可以直接定位像素数据

	//位图信息头
	bi.biSize = sizeof(BITMAPINFOHEADER);	//说明BITMAPINFOHEADER结构体所需的字节数
	bi.biWidth = width;//图像宽度，以像素为单位
	bi.biHeight = height;
	bi.biPlanes = 1;//为目标设备说明位面数，其中总是被设为1
	//bi.biBitCount = 8;//说明比特数/像素的颜色深度，值为0,1,4,8,16,24或32。256灰度级的颜色深度为8，因为2^8 = 256
	bi.biBitCount = 24;//说明比特数/像素的颜色深度，值为0,1,4,8,16,24或32。256灰度级的颜色深度为8，因为2^8 = 256
	bi.biCompression = BI_RGB;//说明图像数据压缩类型
	//bi.biSizeImage = bmp_data_size;//说明图像的大小，一字节为单位
	bi.biSizeImage = rgb_data_size;//说明图像的大小，一字节为单位
	bi.biXPelsPerMeter = 0;//水平分辨率，可以设置为0
	bi.biYPelsPerMeter = 0;//垂直分辨率，可以设置为0
	//bi.biClrUsed = 256;//说明位图实际使用的彩色表中颜色索引数
	bi.biClrUsed = 16777216;//说明位图实际使用的彩色表中颜色索引数
	bi.biClrImportant = 0;//说明对图像显示有重要影响的颜色索引数目，为0表示都重要

	write(bmp_file, &bf, sizeof(bf.bfType));
	write(bmp_file, &bf.bfSize, sizeof(BITMAPFILEHEADER) - (((char *)&bf.bfSize) - ((char *)&bf)));

	write(bmp_file, &bi, sizeof(BITMAPINFOHEADER));//位图信息头写入

	//RGBQUAD rgb;
#if 0 
	reg.alpha = 0;
	int i;
	for (i = 0; i < 256; i++) {
		rgb.rgbBlue = rgb.rgbGreen = rgb.rgbRed = i;
		write(bmp_file, &rgb, sizeof(RGBQUAD));
	}
	
	write(bmp_file, bmp_data, bmp_data_size);
#endif

	if (sq_type == SQ_BG) {
		raw8bits_to_rgb24bits(bmp_data, rgb_data, width, height);
	}


	{
	int w_len = rgb_data_size;
	int w_off = 0;
	//printf("position %l to write bmp data\n", ftell(bmp_file));
	while (w_len > 0) {
       		int len = write(bmp_file, rgb_data + w_off, w_len);
		if (len <= 0)
			break;
		printf("write bmp success len:%d, total:%d\n", len, w_len);
		w_len -= len;
		w_off += len;
	}
	}

#if 1
	printf("from raw file:\n");
	for (int i = 0; i < 24; i++) {
		printf("%x ", raw[i]);
	}
	printf("\n~~~~~~~~~~~~~~~~~~~\n");

	printf("from 8bits raw file:\n");
	for (int i = 0; i < 24; i++) {
		printf("%x ", bmp_data[i]);
	}
	printf("\n~~~~~~~~~~~~~~~~~~~\n");

	printf("from 24bits rgb file:\n");
	for (int i = 0; i < 24; i++) {
		printf("%x ", rgb_data[i]);
	}
	printf("\n~~~~~~~~~~~~~~~~~~~\n");
#endif

close_bmp_file:
	close(bmp_file);
close_raw_file:
	close(raw_file);
	//sync();
free_rgb_data:
	free(rgb_data);
free_bmp:
	free(bmp_data);
free_raw:
	free(raw);
free_file_name:
	if (raw_file_name) {
		free(raw_file_name);
		raw_file_name = NULL;
	}
	if (bmp_file_name) {
		free(bmp_file_name);
		bmp_file_name = NULL;
	}
end:
	return ret;
}

