#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <png.h>

typedef struct {
    unsigned char r, g, b;
} Pixel;

typedef struct {
    int width, height;
    Pixel *pixels;
} Image;

typedef struct {
    int start_row;
    int end_row;
    Image *img_in;
    Image *img_out;
} ThreadData;

int compare(const void *a, const void *b) {
    return (*(unsigned char *)a - *(unsigned char *)b);
}

Pixel medianFilter(Pixel *window) {
    unsigned char r[9], g[9], b[9];
    for (int i = 0; i < 9; i++) {
        r[i] = window[i].r;
        g[i] = window[i].g;
        b[i] = window[i].b;
    }

    qsort(r, 9, sizeof(unsigned char), compare);
    qsort(g, 9, sizeof(unsigned char), compare);
    qsort(b, 9, sizeof(unsigned char), compare);

    Pixel median;
    median.r = r[4]; 
    median.g = g[4]; 
    median.b = b[4]; 
    return median;
}

void* threadMedianFilter(void *arg) {
    ThreadData *data = (ThreadData *)arg;
    Image *img_in = data->img_in;
    Image *img_out = data->img_out;
    int width = img_in->width;

    for (int y = data->start_row; y < data->end_row; y++) {
        for (int x = 1; x < width - 1; x++) {
            Pixel window[9];
            int idx = 0;
            for (int j = -1; j <= 1; j++) {
                for (int i = -1; i <= 1; i++) {
                    window[idx++] = img_in->pixels[(y + j) * width + (x + i)];
                }
            }
            img_out->pixels[y * width + x] = medianFilter(window);
        }
    }
    return NULL;
}

void applyMedianFilterParallel(Image *img_in, Image *img_out, int num_threads) {
    pthread_t threads[num_threads];
    ThreadData thread_data[num_threads];
    
    int rows_per_thread = img_in->height / num_threads;

    for (int i = 0; i < num_threads; i++) {
        thread_data[i].start_row = i * rows_per_thread;
        thread_data[i].end_row = (i == num_threads - 1) ? img_in->height : (i + 1) * rows_per_thread;
        thread_data[i].img_in = img_in;
        thread_data[i].img_out = img_out;
        pthread_create(&threads[i], NULL, threadMedianFilter, &thread_data[i]);
    }

    for (int i = 0; i < num_threads; i++) {
        pthread_join(threads[i], NULL);
    }
}

// Função para carregar a imagem PNG
Image *loadImage(const char *filename) {
    FILE *fp = fopen(filename, "rb");
    if (!fp) {
        perror("Falha ao abrir arquivo PNG");
        return NULL;
    }

    png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (!png) {
        fclose(fp);
        return NULL;
    }

    png_infop info = png_create_info_struct(png);
    if (!info) {
        png_destroy_read_struct(&png, NULL, NULL);
        fclose(fp);
        return NULL;
    }

    if (setjmp(png_jmpbuf(png))) {
        png_destroy_read_struct(&png, &info, NULL);
        fclose(fp);
        return NULL;
    }

    png_init_io(png, fp);
    png_read_info(png, info);

    int width = png_get_image_width(png, info);
    int height = png_get_image_height(png, info);
    png_byte color_type = png_get_color_type(png, info);
    png_byte bit_depth = png_get_bit_depth(png, info);

    if (bit_depth == 16)
        png_set_strip_16(png);
    if (color_type == PNG_COLOR_TYPE_PALETTE)
        png_set_palette_to_rgb(png);
    if (color_type == PNG_COLOR_TYPE_GRAY && bit_depth < 8)
        png_set_expand_gray_1_2_4_to_8(png);
    if (png_get_valid(png, info, PNG_INFO_tRNS))
        png_set_tRNS_to_alpha(png);

    png_set_filler(png, 0xFF, PNG_FILLER_AFTER);
    png_read_update_info(png, info);

    Image *img = (Image *)malloc(sizeof(Image));
    img->width = width;
    img->height = height;
    img->pixels = (Pixel *)malloc(width * height * sizeof(Pixel));

    png_bytep row_pointers[height];
    for (int y = 0; y < height; y++) {
        row_pointers[y] = (png_bytep)malloc(png_get_rowbytes(png, info));
    }

    png_read_image(png, row_pointers);

    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            png_bytep px = &(row_pointers[y][x * 4]);
            img->pixels[y * width + x].r = px[0];
            img->pixels[y * width + x].g = px[1];
            img->pixels[y * width + x].b = px[2];
        }
    }

    fclose(fp);
    png_destroy_read_struct(&png, &info, NULL);
    return img;
}

// Função para salvar a imagem PNG
void saveImage(const char *filename, Image *img) {
    FILE *fp = fopen(filename, "wb");
    if (!fp) {
        perror("Falha ao abrir arquivo para escrita");
        return;
    }

    png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (!png) {
        fclose(fp);
        return;
    }

    png_infop info = png_create_info_struct(png);
    if (!info) {
        png_destroy_write_struct(&png, NULL);
        fclose(fp);
        return;
    }

    if (setjmp(png_jmpbuf(png))) {
        png_destroy_write_struct(&png, &info);
        fclose(fp);
        return;
    }

    png_init_io(png, fp);

    png_set_IHDR(
        png,
        info,
        img->width, img->height,
        8,
        PNG_COLOR_TYPE_RGBA,
        PNG_INTERLACE_NONE,
        PNG_COMPRESSION_TYPE_DEFAULT,
        PNG_FILTER_TYPE_DEFAULT
    );

    png_write_info(png, info);

    png_bytep row_pointers[img->height];
    for (int y = 0; y < img->height; y++) {
        row_pointers[y] = (png_bytep)malloc(img->width * 4 * sizeof(png_byte));
        for (int x = 0; x < img->width; x++) {
            Pixel *pixel = &(img->pixels[y * img->width + x]);
            row_pointers[y][x * 4] = pixel->r;
            row_pointers[y][x * 4 + 1] = pixel->g;
            row_pointers[y][x * 4 + 2] = pixel->b;
            row_pointers[y][x * 4 + 3] = 0xFF;
        }
    }

    png_write_image(png, row_pointers);
    png_write_end(png, NULL);

    for (int y = 0; y < img->height; y++) {
        free(row_pointers[y]);
    }

    fclose(fp);
    png_destroy_write_struct(&png, &info);
}

int main() {
    const char *input_file = "foto_trabalho.png";
    const char *output_file = "imagem_filtrada.png";
    int num_threads = 4;

    Image *image = loadImage(input_file);
    if (!image) {
        return 1;
    }

    Image *filtered_image = (Image *)malloc(sizeof(Image));
    filtered_image->width = image->width;
    filtered_image->height = image->height;
    filtered_image->pixels = (Pixel *)malloc(image->width * image->height * sizeof(Pixel));

    applyMedianFilterParallel(image, filtered_image, num_threads);

    saveImage(output_file, filtered_image);

    free(image->pixels);
    free(image);
    free(filtered_image->pixels);
    free(filtered_image);

    printf("Filtro de mediana aplicado com threads!\n");
    return 0;
}
