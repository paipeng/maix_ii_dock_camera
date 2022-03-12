
#include <unistd.h>
#include <stdint.h>
#include <signal.h>
#include <string.h>
#include <stdlib.h>
#include "time.h"

#include "global_config.h"
#include "libmaix_debug.h"
#include "libmaix_err.h"
#include "libmaix_cam.h"
#include "libmaix_image.h"
#include "libmaix_disp.h"

// #include "rotate.h"


#undef CONFIG_ARCH_V831

#define CAPTURE_IMAGE_WIDTH 640
#define CAPTURE_IMAGE_HEIGHT 480

char fps_str[32];

#define CALC_FPS(tips)                                                                                     \
  {                                                                                                        \
    static int fcnt = 0;                                                                                   \
    fcnt++;                                                                                                \
    static struct timespec ts1, ts2;                                                                       \
    clock_gettime(CLOCK_MONOTONIC, &ts2);                                                                  \
    if ((ts2.tv_sec * 1000 + ts2.tv_nsec / 1000000) - (ts1.tv_sec * 1000 + ts1.tv_nsec / 1000000) >= 1000) \
    {                                                                                                      \
      printf("%s => H26X FPS:%d\n", tips, fcnt);                                                  \
      ts1 = ts2;                                                                                           \
      fcnt = 0;                                                                                            \
    }                                                                                                      \
  }

#define CALC_FPS_DISPLAY(tips, image)                                                                                     \
  {                                                                                                        \
    static int fcnt = 0;                                                                                   \
    fcnt++;                                                                                                \
    static struct timespec ts1, ts2;                                                                       \
    clock_gettime(CLOCK_MONOTONIC, &ts2);                                                                  \
    if ((ts2.tv_sec * 1000 + ts2.tv_nsec / 1000000) - (ts1.tv_sec * 1000 + ts1.tv_nsec / 1000000) >= 1000) \
    {                                                                                                      \
      printf("%s => H26X FPS:%d\n", tips, fcnt);                                                  \
      snprintf(fps_str, sizeof(char)*32, "FPS: %02d", fcnt);						\
      libmaix_cv_image_draw_string(image, 0, 0, fps_str, 1.5, MaixColor(255, 0, 0), 1);			\
      ts1 = ts2;                                                                                           \
      fcnt = 0;                                                                                            \
    }                                                                                                      \
  }


// static struct timeval old, now;

// void cap_set()
// {
//   gettimeofday(&old, NULL);
// }

// void cap_get(const char *tips)
// {
//   gettimeofday(&now, NULL);
//   if (now.tv_usec > old.tv_usec)
//     printf("%20s - %5ld us\r\n", tips, (now.tv_usec - old.tv_usec));
// }

/******************************************************
 *YUV422：Y：U：V=2:1:1
 *RGB24 ：B G R
******************************************************/
int YUV422PToRGB24(void *RGB24, void *YUV422P, int width, int height)
{
  unsigned char *src_y = (unsigned char *)YUV422P;
  unsigned char *src_u = (unsigned char *)YUV422P + width * height;
  unsigned char *src_v = (unsigned char *)YUV422P + width * height * 3 / 2;

  unsigned char *dst_RGB = (unsigned char *)RGB24;

  int temp[3];

  if (RGB24 == NULL || YUV422P == NULL || width <= 0 || height <= 0)
  {
    printf(" YUV422PToRGB24 incorrect input parameter!\n");
    return -1;
  }

  for (int y = 0; y < height; y++)
  {
    for (int x = 0; x < width; x++)
    {
      int Y = y * width + x;
      int U = Y >> 1;
      int V = U;

      temp[0] = src_y[Y] + ((7289 * src_u[U]) >> 12) - 228;                             //b
      temp[1] = src_y[Y] - ((1415 * src_u[U]) >> 12) - ((2936 * src_v[V]) >> 12) + 136; //g
      temp[2] = src_y[Y] + ((5765 * src_v[V]) >> 12) - 180;                             //r

      dst_RGB[3 * Y] = (temp[0] < 0 ? 0 : temp[0] > 255 ? 255
                                                        : temp[0]);
      dst_RGB[3 * Y + 1] = (temp[1] < 0 ? 0 : temp[1] > 255 ? 255
                                                            : temp[1]);
      dst_RGB[3 * Y + 2] = (temp[2] < 0 ? 0 : temp[2] > 255 ? 255
                                                            : temp[2]);
    }
  }

  return 0;
}

struct {
  int w0, h0;
  struct libmaix_cam *cam0;
  #ifdef CONFIG_ARCH_V831 // CONFIG_ARCH_V831 & CONFIG_ARCH_V833
  struct libmaix_cam *cam1;
  #endif
  uint8_t *rgb888;

  struct libmaix_disp *disp;

  int is_run;
} test = { 0 };

static void test_handlesig(int signo)
{
  if (SIGINT == signo || SIGTSTP == signo || SIGTERM == signo || SIGQUIT == signo || SIGPIPE == signo || SIGKILL == signo)
  {
    test.is_run = 0;
  }
  // exit(0);
}

inline static unsigned char make8color(unsigned char r, unsigned char g, unsigned char b)
{
	return (
	(((r >> 5) & 7) << 5) |
	(((g >> 5) & 7) << 2) |
	 ((b >> 6) & 3)	   );
}

inline static unsigned short make16color(unsigned char r, unsigned char g, unsigned char b)
{
	return (
	(((r >> 3) & 31) << 11) |
	(((g >> 2) & 63) << 5)  |
	 ((b >> 3) & 31)		);
}

void test_init() {

  libmaix_camera_module_init();

  test.w0 = CAPTURE_IMAGE_WIDTH, test.h0 = CAPTURE_IMAGE_HEIGHT;
  //test.w0 = 480, test.h0 = 480;

  test.cam0 = libmaix_cam_create(0, test.w0, test.h0, 1, 0);
  if (NULL == test.cam0) return ;  test.rgb888 = (uint8_t *)malloc(test.w0 * test.h0 * 3);

  #ifdef CONFIG_ARCH_V831 // CONFIG_ARCH_V831 & CONFIG_ARCH_V833
  test.cam1 = libmaix_cam_create(1, test.w0, test.h0, 0, 0);
  if (NULL == test.cam0) return ;  test.rgb888 = (uint8_t *)malloc(test.w0 * test.h0 * 3);
  #endif

  test.disp = libmaix_disp_create(0);
  if(NULL == test.disp) return ;

  test.is_run = 1;

  // ALOGE(__FUNCTION__);
}

void test_exit() {

  if (NULL != test.cam0) libmaix_cam_destroy(&test.cam0);

  #ifdef CONFIG_ARCH_V831 // CONFIG_ARCH_V831 & CONFIG_ARCH_V833
  if (NULL != test.cam1) libmaix_cam_destroy(&test.cam1);
  #endif

  if (NULL != test.rgb888) free(test.rgb888), test.rgb888 = NULL;
  if (NULL != test.disp) libmaix_disp_destroy(&test.disp), test.disp = NULL;

  libmaix_camera_module_deinit();

  // ALOGE(__FUNCTION__);
}

void test_work() {

  printf("start_capture...\n");
  test.cam0->start_capture(test.cam0);

  #ifdef CONFIG_ARCH_V831 // CONFIG_ARCH_V831 & CONFIG_ARCH_V833
  test.cam1->start_capture(test.cam1);
  #endif
  while (test.is_run)
  {
    // goal code
    libmaix_image_t *tmp = NULL;
    if (LIBMAIX_ERR_NONE == test.cam0->capture_image(test.cam0, &tmp))
    {
        printf("w %d h %d p %d \r\n", tmp->width, tmp->height, tmp->mode);
        if (tmp->width == test.disp->width && test.disp->height == tmp->height) {
            //libmaix_cv_image_draw_string(tmp, 0, 0, "FPS", 1.5, MaixColor(255, 0, 0), 1);
            test.disp->draw_image(test.disp, tmp);
        } else {
#if 1
            int resize_width = test.disp->width;
            int resize_height = test.disp->width*CAPTURE_IMAGE_HEIGHT/CAPTURE_IMAGE_WIDTH;
            printf("resize: w %d h %d \n", resize_width, resize_height);
            libmaix_image_t *rs = libmaix_image_create(resize_width, resize_height, LIBMAIX_IMAGE_MODE_RGB888, LIBMAIX_IMAGE_LAYOUT_HWC, NULL, true);
            if (rs) {
                printf("resize: %d-%d -> %d-%d\n", tmp->width, tmp->height, resize_width, resize_height);
                libmaix_cv_image_resize(tmp, resize_width, resize_height, &rs);
                libmaix_cv_image_draw_string(rs, 0, 0, fps_str, 1.5, MaixColor(255, 0, 0), 1);
                CALC_FPS_DISPLAY("maix_cam 0", rs);
                test.disp->draw_image(test.disp, rs);
                libmaix_image_destroy(&rs);
            }
#endif
        }
        //CALC_FPS("maix_cam 0");

        #ifdef CONFIG_ARCH_V831 // CONFIG_ARCH_V831 & CONFIG_ARCH_V833
        libmaix_image_t *t = NULL;
        if (LIBMAIX_ERR_NONE == test.cam1->capture_image(test.cam1, &t))
        {
            printf("w %d h %d p %d \r\n", t->width, t->height, t->mode);
            CALC_FPS("maix_cam 1");
        }
        #endif
    }
  }
}

int main(int argc, char **argv)
{
  signal(SIGINT, test_handlesig);
  signal(SIGTERM, test_handlesig);

  libmaix_image_module_init();

  test_init();
  test_work();
  test_exit();

  libmaix_image_module_deinit();

  return 0;
}
