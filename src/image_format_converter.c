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
	ERRNO_NOERR = 0,
	ERRNO_OTHERS,
	ERRNO_NOMEM,
	ERRNO_OPEN_FILE_FAILED,
	ERRNO_WRITE_FAILED,
	ERRNO_ARGUMENT_ERR,
} IFC_ERRNO;

typedef enum {
	RAW_SQ_TYPE_START = 0,
	RAW_SQ_TYPE_BGGR,
	RAW_SQ_TYPE_END
} IFC_RAW_SQ_TYPE;

typedef enum {
	CONV_TYPE_START = 0,
	CONV_TYPE_RAW10BITS_BGGR_TO_BMP24BITS,
	CONV_TYPE_END,
} IFC_CONV_TYPE;

typedef struct tagIFC_BMP_BITMAPFILEHEADER {
	u16 bfType;
	u32 bfSize;
	u16 bfReserved1;
	u16 bfReserved2;
	u32 bfOffBits;
} IFC_BMP_BITMAPFILEHEADER;

typedef enum {
	BI_RGB = 0,
	BI_RLE8,
	BI_BLE4,
	BI_BITFILEDS,
	BI_JPEG,
	BI_PNG,
} IFC_BMP_BI_COMPRESSION;

typedef struct tagIFC_BMP_BITMAPINFOHEADER {
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
} IFC_BMP_BITMAPINFOHEADER;

typedef struct tagIFC_BMP_RGBQUAD {
	u8 rgbBlue;
	u8 rgbGreen;
	u8 rgbRed;
	u8 alpha;
} IFC_BMP_RGBQUAD;

static void parse_args(int argc, char **argv, int *width, int *height, IFC_CONV_TYPE *conv_type, char **input_file_name)
{
	int ch;
	opterr = 0;

	while ((ch = getopt(argc, argv, "w:h:t:f:")) != -1){
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

		case 't':
			*conv_type = atoi(optarg);
			break;

		case 'f':
			*input_file_name = (char *)malloc(strlen((const char *)optarg) + 1);
			if (!*input_file_name) {
				printf("malloc file name for input_file_name failed\n");
				break;
			}
			strcpy(*input_file_name, optarg);
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

static int malloc_output_file_name(const char *input_file_name, char **output_file_name, IFC_CONV_TYPE conv_type)
{
	#define SUFFIX_MAX_LEN 16
	IFC_ERRNO ret = ERRNO_NOERR;
	int first_name_len;
	char output_file_name_suffix[SUFFIX_MAX_LEN];
	char *p;

	printf("input_file_name:%s \n", input_file_name);

	switch (conv_type) {
	case CONV_TYPE_RAW10BITS_BGGR_TO_BMP24BITS:
		strcpy(output_file_name_suffix, ".bmp");
		break;

	default:
		strcpy(output_file_name_suffix, ".suffix");
		break;
		
	}

	p = strrchr(input_file_name, '.');
	if (p) {
		first_name_len = p - input_file_name;
	} else {
		first_name_len = strlen(input_file_name);
	}

	*output_file_name = (char *)malloc(first_name_len + SUFFIX_MAX_LEN);
	if (*output_file_name) {
		strncpy(*output_file_name, input_file_name, first_name_len);
		*(*output_file_name + first_name_len) = '\0';
		strcat(*output_file_name, output_file_name_suffix);
	} else {
		printf("[%s] malloc for output_file_name failed\n", __func__);
		ret = ERRNO_NOMEM;
	}

	return ret;
}

static IFC_ERRNO raw2bmp(int input_file_fd, int width, int height, int input_raw_bits_per_pixl,
			IFC_RAW_SQ_TYPE raw_sq_type, int output_file_fd)
{
	const int result_raw_bits_per_pixl = 8;
	int raw_data_size;
	int result_raw_data_size;
	int rgb_data_size;
	IFC_ERRNO ret = ERRNO_NOERR;
	IFC_BMP_BITMAPFILEHEADER bf;
	IFC_BMP_BITMAPINFOHEADER bi;
	IFC_BMP_RGBQUAD rgb;
	unsigned char *raw;
	unsigned char *result_raw;
	unsigned char *rgb_data;

	raw_data_size = width * height * input_raw_bits_per_pixl / 8;
	raw = (unsigned char *)malloc(raw_data_size);
	if (!raw) {
		printf("malloc for raw failed\n");
		ret = ERRNO_NOMEM;
		goto end;
	}
	memset(raw, 0, raw_data_size);
	read(input_file_fd, raw, raw_data_size);

	result_raw_data_size = width * height * 8 / result_raw_bits_per_pixl;
	result_raw = (unsigned char *)malloc(result_raw_data_size);
	if (!result_raw) {
		printf("malloc for result_raw failed\n");
		ret = ERRNO_NOMEM;
		goto free_raw;
	}
	memset(result_raw, 0, result_raw_data_size);

	rgb_data_size = result_raw_data_size * 3;
	rgb_data = (unsigned char *)malloc(rgb_data_size);
	if (!rgb_data) {
		printf("malloc for rgb data failed\n");
		ret = ERRNO_NOMEM;
		goto free_result_raw;
	}
	memset(rgb_data, 0, rgb_data_size);

	if (input_raw_bits_per_pixl != result_raw_bits_per_pixl) {
		/* 处理为8bits位宽，即使 10/12bits --> 8bits */
		int i;
		int pixels = width * height;
		for (i = 0; i < pixels; i++) {
			int off_byte = input_raw_bits_per_pixl * i / 8;
			int off_bits = input_raw_bits_per_pixl * i % 8;
			int value = (*(raw + off_byte)) >> off_bits;

			u8 next_byte_value = *(raw + off_byte + 1);
			int next_byte_off_bits = input_raw_bits_per_pixl - (8 - off_bits);
			u8 tmp = next_byte_value << (8 - next_byte_off_bits);
			tmp >>= (8 - next_byte_off_bits);
			value += tmp << (8 - off_bits);

			*(result_raw + i) = value * (1 << result_raw_bits_per_pixl) / (1 << input_raw_bits_per_pixl);
		}
	} else {
		int i;
		int pixels = width * height;
		for (i = 0; i < pixels; i++) {
			*(result_raw + i) = *(raw + i);
		}
	}

	//位图头文件（包含有关文件类型，大小，存放位置等信息）
	bf.bfType = ((u16)('M' << 8) | 'B');	//"BM"说明文件类型
	bf.bfReserved1 = 0;
	bf.bfReserved2 = 0;
	//bf.bfSize = sizeof(IFC_BMP_BITMAPFILEHEADER) - 2 + sizeof(IFC_BMP_BITMAPINFOHEADER) + sizeof(IFC_BMP_RGBQUAD) * 256 + result_raw_data_size;//文件大小
	bf.bfSize = sizeof(IFC_BMP_BITMAPFILEHEADER) - 2 + sizeof(IFC_BMP_BITMAPINFOHEADER) + rgb_data_size;//文件大小
	bf.bfOffBits = sizeof(IFC_BMP_BITMAPFILEHEADER) - 2 + sizeof(IFC_BMP_BITMAPINFOHEADER);//表示从头文件开始到实际图像数据之间的字节的偏移，bfOffBits可以直接定位像素数据

	//位图信息头
	bi.biSize = sizeof(IFC_BMP_BITMAPINFOHEADER);	//说明BITMAPINFOHEADER结构体所需的字节数
	bi.biWidth = width;//图像宽度，以像素为单位
	bi.biHeight = height;
	bi.biPlanes = 1;//为目标设备说明位面数，其中总是被设为1
	//bi.biBitCount = 8;//说明比特数/像素的颜色深度，值为0,1,4,8,16,24或32。256灰度级的颜色深度为8，因为2^8 = 256
	bi.biBitCount = 24;//说明比特数/像素的颜色深度，值为0,1,4,8,16,24或32。256灰度级的颜色深度为8，因为2^8 = 256
	bi.biCompression = BI_RGB;//说明图像数据压缩类型
	//bi.biSizeImage = result_raw_data_size;//说明图像的大小，一字节为单位
	bi.biSizeImage = rgb_data_size;//说明图像的大小，一字节为单位
	bi.biXPelsPerMeter = 0;//水平分辨率，可以设置为0
	bi.biYPelsPerMeter = 0;//垂直分辨率，可以设置为0
	//bi.biClrUsed = 256;//说明位图实际使用的彩色表中颜色索引数
	bi.biClrUsed = 16777216;//说明位图实际使用的彩色表中颜色索引数
	bi.biClrImportant = 0;//说明对图像显示有重要影响的颜色索引数目，为0表示都重要

	write(output_file_fd, &bf, sizeof(bf.bfType));
	write(output_file_fd, &bf.bfSize, sizeof(IFC_BMP_BITMAPFILEHEADER) - (((char *)&bf.bfSize) - ((char *)&bf)));

	write(output_file_fd, &bi, sizeof(IFC_BMP_BITMAPINFOHEADER));//位图信息头写入

	//IFC_BMP_RGBQUAD rgb;
#if 0 
	reg.alpha = 0;
	int i;
	for (i = 0; i < 256; i++) {
		rgb.rgbBlue = rgb.rgbGreen = rgb.rgbRed = i;
		write(output_file_fd, &rgb, sizeof(IFC_BMP_RGBQUAD));
	}
	
	write(output_file_fd, result_raw, result_raw_data_size);
#endif

	switch (raw_sq_type) {
	case RAW_SQ_TYPE_BGGR:
		raw8bits_to_rgb24bits(result_raw, rgb_data, width, height);
		break;

	default:
		break;
	}


	{
	int w_len = rgb_data_size;
	int w_off = 0;
	//printf("position %l to write bmp data\n", ftell(output_file_fd));
	while (w_len > 0) {
       		int len = write(output_file_fd, rgb_data + w_off, w_len);
		if (len <= 0) {
			ret = ERRNO_WRITE_FAILED;
			break;
		}
		printf("write bmp success len:%d, total:%d\n", len, w_len);
		w_len -= len;
		w_off += len;
	}
	}

	free(rgb_data);
free_result_raw:
	free(result_raw);
free_raw:
	free(raw);
end:
	return ret;
}

/*
 * -w input image width
 * -h input image heighth
 * -t indicated input image format and output image format, refer to IFC_CONV_TYPE
 * -f input file name
 */
int main(int argc, char **argv)
{
	int input_file_fd;
	int output_file_fd;
	int width, height;
	IFC_ERRNO ret = ERRNO_NOERR;
	IFC_CONV_TYPE conv_type;
	char *input_file_name = NULL;
	char *output_file_name = NULL;

	parse_args(argc, argv, &width, &height, &conv_type, &input_file_name);
	if ((conv_type <= CONV_TYPE_START) || (conv_type >= CONV_TYPE_END)) {
		printf("Don't support conv_type:%d\n", conv_type);
		ret = ERRNO_ARGUMENT_ERR;
		goto free_file_name;
	}
	if (!input_file_name) {
		printf("please input input_file_name\n");
		ret = ERRNO_ARGUMENT_ERR;
		goto free_file_name;
	}

	ret = malloc_output_file_name(input_file_name, &output_file_name, conv_type);
	if (ret != ERRNO_NOERR) {
		printf("malloc_output_file_name failed\n");
		goto free_file_name;
	}

	input_file_fd = open(input_file_name, O_RDONLY);
	if (input_file_fd == -1) {
		printf("open input file failed\n");
		ret = ERRNO_OPEN_FILE_FAILED;
		goto free_file_name;
	}

	output_file_fd = open(output_file_name, O_RDWR | O_CREAT | O_TRUNC, S_IRWXU/* | S_IRWXG | S_IRWXO*/);
	if (output_file_fd == -1) {
		printf("open output file:%s failed\n", output_file_name);
		ret = ERRNO_OPEN_FILE_FAILED;
		goto close_input_file_fd;
	}

	switch (conv_type) {
	case CONV_TYPE_RAW10BITS_BGGR_TO_BMP24BITS:
		ret = raw2bmp(input_file_fd, width, height, 10, RAW_SQ_TYPE_BGGR, output_file_fd);
		break;

	default:
		ret = ERRNO_ARGUMENT_ERR;
		break;
	}

	close(output_file_fd);
close_input_file_fd:
	close(input_file_fd);
	//sync();
free_file_name:
	if (input_file_name) {
		free(input_file_name);
		input_file_name = NULL;
	}
	if (output_file_name) {
		free(output_file_name);
		output_file_name = NULL;
	}
end:
	return ret;
}

