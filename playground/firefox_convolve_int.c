// Int-only Firefox-style convolve benchmark for the LLVM taint pass.
//
// This is the integer path from firefox_convolve.c with the floating-point
// convolution code removed.

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NOINLINE __attribute__((noinline))

static inline unsigned umin(unsigned a, unsigned b) {
  return a - ((a - b) & -(a > b));
}

enum {
  B8G8R8A8_COMPONENT_BYTEOFFSET_B = 0,
  B8G8R8A8_COMPONENT_BYTEOFFSET_G = 1,
  B8G8R8A8_COMPONENT_BYTEOFFSET_R = 2,
  B8G8R8A8_COMPONENT_BYTEOFFSET_A = 3,
};

static inline uint8_t color_component_int(const uint8_t *data,
                                          int32_t stride, int32_t x,
                                          int32_t y, int32_t bpp,
                                          int32_t c) {
  return data[y * stride + bpp * x + c];
}

static inline int32_t clamp_to_nonzero(int32_t a) {
  return a * (a >= 0);
}

NOINLINE static void convolve_pixel_int(
    const uint8_t *source_data, uint8_t *target_data, int32_t width,
    int32_t height, int32_t source_stride, int32_t target_stride, int32_t x,
    int32_t y, const int32_t *kernel, int32_t bias, int32_t shift_l,
    int32_t shift_r, int preserve_alpha, int32_t order_x, int32_t order_y,
    int32_t target_x, int32_t target_y, int32_t kernel_unit_length_x,
    int32_t kernel_unit_length_y) {
  (void)width;
  (void)height;

  int32_t sum[4] = {0, 0, 0, 0};
  int32_t offsets[4] = {
      B8G8R8A8_COMPONENT_BYTEOFFSET_R,
      B8G8R8A8_COMPONENT_BYTEOFFSET_G,
      B8G8R8A8_COMPONENT_BYTEOFFSET_B,
      B8G8R8A8_COMPONENT_BYTEOFFSET_A,
  };
  int32_t channels = preserve_alpha ? 3 : 4;
  int32_t rounding_addition = shift_l == 0 ? 0 : 1 << (shift_l - 1);

  for (int32_t ky = 0; ky < order_y; ky++) {
    int32_t sample_y = y + (ky - target_y) * kernel_unit_length_y;
    for (int32_t kx = 0; kx < order_x; kx++) {
      int32_t sample_x = x + (kx - target_x) * kernel_unit_length_x;
      int32_t coeff = kernel[order_x * ky + kx];
      for (int32_t i = 0; i < channels; i++) {
        sum[i] += coeff * color_component_int(source_data, source_stride,
                                              sample_x, sample_y, 4,
                                              offsets[i]);
      }
    }
  }

  for (int32_t i = 0; i < channels; i++) {
    int32_t clamped =
        umin(clamp_to_nonzero(sum[i] + bias), 255 << shift_l >> shift_r);
    target_data[y * target_stride + 4 * x + offsets[i]] =
        (clamped + rounding_addition) << shift_r >> shift_l;
  }

  if (preserve_alpha) {
    target_data[y * target_stride + 4 * x + B8G8R8A8_COMPONENT_BYTEOFFSET_A] =
        source_data[y * source_stride + 4 * x +
                    B8G8R8A8_COMPONENT_BYTEOFFSET_A];
  }
}

NOINLINE static void run_kernel_int(int width, int height, int kernel_size,
                                    int iterations, const uint8_t *source,
                                    uint8_t *target, const int32_t *kernel,
                                    int32_t shift_l, int32_t shift_r) {
  int32_t stride = width * 4;
  int32_t target_x = kernel_size / 2;
  int32_t target_y = kernel_size / 2;
  int margin = kernel_size / 2 + 1;

  for (int iter = 0; iter < iterations; iter++) {
    for (int32_t y = margin; y < height - margin; y++) {
      for (int32_t x = margin; x < width - margin; x++) {
        convolve_pixel_int(source, target, width, height, stride, stride, x, y,
                           kernel, 0, shift_l, shift_r, 0, kernel_size,
                           kernel_size, target_x, target_y, 1, 1);
      }
    }
  }
}

static uint64_t checksum(const uint8_t *buf, size_t size) {
  uint64_t sum = 0;
  for (size_t i = 0; i < size; i++)
    sum = (sum * 131) + buf[i];
  return sum;
}

static int parse_positive_int(const char *value, int fallback) {
  int parsed = atoi(value);
  return parsed > 0 ? parsed : fallback;
}

int main(int argc, char *argv[]) {
  int width = 256;
  int height = 256;
  int kernel_size = 3;
  int iterations = 50;
  int warmup_iterations = 5;

  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--width") == 0 && i + 1 < argc) {
      width = parse_positive_int(argv[++i], width);
    } else if (strcmp(argv[i], "--height") == 0 && i + 1 < argc) {
      height = parse_positive_int(argv[++i], height);
    } else if (strcmp(argv[i], "--kernel") == 0 && i + 1 < argc) {
      kernel_size = parse_positive_int(argv[++i], kernel_size);
    } else if (strcmp(argv[i], "--iter") == 0 && i + 1 < argc) {
      iterations = parse_positive_int(argv[++i], iterations);
    } else if (strcmp(argv[i], "--warmup") == 0 && i + 1 < argc) {
      warmup_iterations = parse_positive_int(argv[++i], warmup_iterations);
    }
  }

  if (kernel_size < 1)
    kernel_size = 1;
  if ((kernel_size & 1) == 0)
    kernel_size++;

  int margin = kernel_size / 2 + 1;
  if (width <= margin * 2)
    width = margin * 2 + 1;
  if (height <= margin * 2)
    height = margin * 2 + 1;

  printf("firefox_convolve_int width=%d height=%d kernel=%d iter=%d warmup=%d\n",
         width, height, kernel_size, iterations, warmup_iterations);

  int32_t stride = width * 4;
  size_t image_size = (size_t)stride * (size_t)height;
  uint8_t *source = (uint8_t *)malloc(image_size);
  uint8_t *target = (uint8_t *)calloc(image_size, 1);
  int32_t *kernel =
      (int32_t *)malloc((size_t)kernel_size * (size_t)kernel_size *
                        sizeof(int32_t));

  if (!source || !target || !kernel) {
    free(source);
    free(target);
    free(kernel);
    fprintf(stderr, "allocation failed\n");
    return 1;
  }

  for (size_t i = 0; i < image_size; i++)
    source[i] = (uint8_t)(i * 17 + 31);
  for (int i = 0; i < kernel_size * kernel_size; i++)
    kernel[i] = 1;

  int32_t shift_l = 0;
  int32_t shift_r = 0;
  int divisor = kernel_size * kernel_size;
  while ((1 << (shift_r + 1)) < divisor)
    shift_r++;

  run_kernel_int(width, height, kernel_size, warmup_iterations, source, target,
                 kernel, shift_l, shift_r);
  run_kernel_int(width, height, kernel_size, iterations, source, target, kernel,
                 shift_l, shift_r);

  printf("checksum=%llu\n", (unsigned long long)checksum(target, image_size));

  free(source);
  free(target);
  free(kernel);
  return 0;
}
