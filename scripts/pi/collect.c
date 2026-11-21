/*
 * collect.c : take labelled test pictures from the hat camera, for laptop testing.
 *
 * How it works:
 *   1. You type what Brim SHOULD say for the scene in front of the camera:
 *        left  right  stop  gap  none        (see the laptop guide for what each means)
 *   2. You get 3 seconds to put the camera back at hat height and hold still.
 *   3. It saves one picture named like  right_007.ppm  in the folder you chose.
 *   4. Repeat. Type q to quit.
 *
 * The label is in the file name, so the laptop script knows the right answer.
 *
 * Privacy (team rule): only teammates who agreed may be in these pictures.
 *
 * Brim must be STOPPED first (only one program can use the camera):
 *     sudo sh ~/brim-run/stop.sh
 * Build on the Pi:   gcc -o collect collect.c -lcamapi
 * Run on the Pi:     ./collect ~/brim_pics
 */

#include <stdio.h>               /* printf, fopen, fgets                              */
#include <stdlib.h>              /* malloc, free                                      */
#include <string.h>              /* strcmp, strcspn, strerror                         */
#include <unistd.h>              /* sleep, usleep                                     */
#include <sys/stat.h>            /* mkdir: make the pictures folder                   */
#include <stdatomic.h>           /* atomic_*: values shared safely between 2 threads  */
#include <camera/camera_api.h>   /* QNX Camera library                                */

#define CAMERA_UNIT   CAMERA_UNIT_5   /* the USB camera slot in sensor_rpi5.conf        */
#define WARMUP_FRAMES 45              /* ignore the first frames: auto exposure is dark */
#define COUNTDOWN_S   3               /* seconds to get into position before the photo  */

/* ---------------------------------------------------------------------------
 * Shared state between main() and the camera's own thread
 * ------------------------------------------------------------------------- */
static atomic_int frames_total = 0;   /* frames seen so far (for the warm up)            */
static atomic_int want_photo   = 0;   /* main sets 1 = "save the next frame"; camera sets 0 when done */
static atomic_int photo_ok     = 0;   /* camera sets 1 if the save worked                */
static char       photo_path[512];    /* where to save; written by main BEFORE want_photo = 1 */

static unsigned char clamp_to_byte(float v) { return v < 0 ? 0 : (v > 255 ? 255 : (unsigned char)v); }

/* Save a YUYV (YCbYCr) frame as a PPM picture: same conversion as capture.c and Brim. */
static int save_ppm(const camera_buffer_t *buf, const char *path) {
    const camera_frame_ycbycr_t *d = &buf->framedesc.ycbycr;
    FILE *f = fopen(path, "wb");
    if (!f) { perror("could not create picture"); return -1; }
    fprintf(f, "P6\n%u %u\n255\n", d->width, d->height);      /* PPM header            */
    unsigned char *rgb = malloc((size_t)d->width * 3);          /* one row of RGB pixels */
    if (!rgb) { fclose(f); return -1; }
    for (uint32_t y = 0; y < d->height; y++) {
        const uint8_t *row = buf->framebuf + (size_t)y * d->stride;
        for (uint32_t x = 0; x + 1 < d->width; x += 2) {        /* 2 pixels = 4 bytes: Y0 Cb Y1 Cr */
            const uint8_t *p = row + (size_t)x * 2;
            float y0 = p[0], cb = p[1] - 128.0f, y1 = p[2], cr = p[3] - 128.0f;
            unsigned char *o = rgb + (size_t)x * 3;
            o[0] = clamp_to_byte(y0 + 1.402f * cr);
            o[1] = clamp_to_byte(y0 - 0.344f * cb - 0.714f * cr);
            o[2] = clamp_to_byte(y0 + 1.772f * cb);
            o[3] = clamp_to_byte(y1 + 1.402f * cr);
            o[4] = clamp_to_byte(y1 - 0.344f * cb - 0.714f * cr);
            o[5] = clamp_to_byte(y1 + 1.772f * cb);
        }
        fwrite(rgb, 1, (size_t)d->width * 3, f);
    }
    free(rgb);
    fclose(f);
    return 0;
}

/* Called by the camera library for EVERY frame. Saves one only when main() asked for it. */
static void on_frame(camera_handle_t h, camera_buffer_t *buf, void *arg) {
    (void)h; (void)arg;
    if (atomic_fetch_add(&frames_total, 1) < WARMUP_FRAMES) return;  /* still warming up */
    if (atomic_load(&want_photo) == 0) return;                        /* nobody asked    */
    int ok = buf->frametype == CAMERA_FRAMETYPE_YCBYCR && save_ppm(buf, photo_path) == 0;
    atomic_store(&photo_ok, ok);
    atomic_store(&want_photo, 0);                                     /* tell main: done */
}

/* True if the label is one the laptop script understands. */
static int valid_label(const char *s) {
    const char *ok[] = {"left", "right", "stop", "gap", "none"};
    for (int i = 0; i < 5; i++) if (strcmp(s, ok[i]) == 0) return 1;
    return 0;
}

int main(int argc, char *argv[]) {
    const char *folder = argc > 1 ? argv[1] : "brim_pics";
    mkdir(folder, 0755);                          /* fine if it already exists */

    camera_handle_t cam = CAMERA_HANDLE_INVALID;
    camera_error_t err = camera_open(CAMERA_UNIT, CAMERA_MODE_RW, &cam);
    if (err != CAMERA_EOK) {
        fprintf(stderr, "camera_open failed: %s\nIs Brim still running? Stop it: sudo sh ~/brim-run/stop.sh\n",
                strerror(err));
        return 1;
    }
    camera_set_vf_property(cam, CAMERA_IMGPROP_CREATEWINDOW, 0);   /* no monitor: no window */
    if (camera_start_viewfinder(cam, on_frame, NULL, NULL) != CAMERA_EOK) {
        fprintf(stderr, "could not start the camera\n");
        camera_close(cam);
        return 1;
    }

    printf("Camera on. Pictures go to %s/\n", folder);
    printf("Labels: left right stop gap none   (q = quit)\n");
    char line[64];
    for (int n = 1;; ) {
        printf("\nWhat SHOULD Brim say for this scene? ");
        fflush(stdout);
        if (!fgets(line, sizeof line, stdin)) break;          /* control+D also quits */
        line[strcspn(line, "\r\n ")] = 0;                     /* cut off Enter and spaces */
        if (strcmp(line, "q") == 0) break;
        if (!valid_label(line)) { printf("  Use one of: left right stop gap none\n"); continue; }

        for (int s = COUNTDOWN_S; s > 0; s--) {              /* time to get into position */
            printf("  %d...\n", s);
            fflush(stdout);
            sleep(1);
        }
        snprintf(photo_path, sizeof photo_path, "%s/%s_%03d.ppm", folder, line, n);
        atomic_store(&want_photo, 1);                        /* ask the camera thread for one frame */
        for (int waited = 0; atomic_load(&want_photo) && waited < 300; waited++) usleep(10000);  /* up to 3 s */
        if (atomic_load(&want_photo) || !atomic_load(&photo_ok)) {
            atomic_store(&want_photo, 0);
            printf("  Picture NOT saved (no frame from the camera). Try again.\n");
            continue;
        }
        printf("  Saved %s\n", photo_path);
        n++;
    }

    camera_stop_viewfinder(cam);
    camera_close(cam);
    printf("Done. Restart Brim with:  sudo sh ~/brim-run/start.sh\n");
    return 0;
}
