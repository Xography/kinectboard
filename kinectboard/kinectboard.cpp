// vim:ts=4:sw=4:expandtab
/*
 * This file is part of the OpenKinect Project. http://www.openkinect.org
 *
 * Copyright (c) 2010 individual OpenKinect contributors. See the CONTRIB file
 * for details.
 *
 * This code is licensed to you under the terms of the Apache License, version
 * 2.0, or, at your option, the terms of the GNU General Public License,
 * version 2.0. See the APACHE20 and GPL2 files for the text of the licenses,
 * or the following URLs:
 * http://www.apache.org/licenses/LICENSE-2.0
 * http://www.gnu.org/licenses/gpl-2.0.txt
 *
 * If you redistribute this file in source form, modified or unmodified, you
 * may:
 *   1) Leave this header intact and distribute it under the same terms,
 *      accompanying it with the APACHE20 and GPL20 files, or
 *   2) Delete the Apache 2.0 clause and accompany it with the GPL2 file, or
 *   3) Delete the GPL v2 clause and accompany it with the APACHE20 file
 * In all cases you must keep the copyright notice intact and include a copy
 * of the CONTRIB file.
 *
 * Binary distributions must follow the binary distribution requirements of
 * either License.
 */


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <libfreenect.h>
#include <stdint.h>
#include <math.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <pthread.h>

#include <SDL/SDL.h>
#include <SDL/SDL_ttf.h>

#include <GL/glew.h>
#include <GL/glut.h>
#include <GL/gl.h>
#include <GL/glu.h>

#include <math.h>

#include <sys/time.h>

#include "kinect.h"
#include "median.h"
#include "glow.h"
#include "maskrgb.h"
#include "kinectboard_ui.h"
#include "cudadeviceinfo.h"

#include "kinectboard_images.h"

// Cuda
#include <cuda.h>
#include <cutil_inline.h>
#include <cuda_gl_interop.h>
#include <drvapi_error_string.h>

// two kinect images (full resolution) next to each other
#define SCREEN_WIDTH (640 * 2)
// kinect image height (480) + space for controls
#define SCREEN_HEIGHT 480 + 300
#define SCREEN_DEPTH 32

GLuint rawDepthBufferID;
GLuint rawRgbBufferID;
GLuint medianBufferID;
GLuint maskedMedianBufferID;
GLuint glowBufferID;
GLuint maskRgbBufferID;
GLuint rawDepthTextureID;
GLuint rawRgbTextureID;
GLuint medianTextureID;
GLuint maskedMedianTextureID;
GLuint glowTextureID;
GLuint maskRgbTextureID;

uint16_t *depth_buffer;

GLuint gl_depth_tex;
GLuint gl_rgb_tex;

extern int ANIMATION_ONE_STEP;

// Die Referenz-Farbe (vom Nutzer ausgewählt). Wird normiert gespeichert:
// Jeder Farbanteil wird durch √(r² + g² + b²) geteilt.
double reference_r = -1;
double reference_g = -1;
double reference_b = -1;

double FILTER_DISTANCE = 0.2f;

double DEPTH_MASK_MULTIPLIER = 0.0f;

int DEPTH_MASK_THRESHOLD = 2;

SDL_Surface *chosen_color_surface;

#if 0
void rgb_cb(freenect_device *dev, void *rgb, uint32_t timestamp)
{
    // swap buffers
    assert (rgb_back == rgb);
    rgb_back = rgb_mid;
    freenect_set_video_buffer(dev, rgb_back);
    rgb_mid = (uint8_t*)rgb;

    /* Wenn es eine Referenzefarbe gibt, filtern wir das Farbbild. */
    if (reference_r != -1) {
        int i;
        for (i = 0; i < 640 * 480; i++) {
            double r = rgb_mid[3 * i + 0];
            double g = rgb_mid[3 * i + 1];
            double b = rgb_mid[3 * i + 2];

            double nom = sqrt((r * r) + (g * g) + (b * b));
            r /= nom;
            g /= nom;
            b /= nom;

            /* depth_mid ist das Tiefenbild. */
            double distance = sqrt(pow((reference_r - r), 2) + pow((reference_g - g), 2) + pow((reference_b - b), 2));

            //printf("median_filtered = %d\n", depth_median_filtered[i]);
            distance += (depth_median_filtered[i] / 255.0) * DEPTH_MASK_MULTIPLIER;

            if (distance > FILTER_DISTANCE) {
                pushrgb(rgb, i, 0, 0, 0);
            }
        }
    }

    static CvMat *rotationsmatrix = NULL;
    static CvMat *translationsmatrix = NULL;
    static CvMat *result = NULL;
    static CvMat *P3D = NULL;
    if (!rotationsmatrix) {
        rotationsmatrix = cvCreateMat(3, 3, CV_32FC1);

        CV_MAT_ELEM(*rotationsmatrix, float, 0, 0) = 9.9984628826577793e-01;
        CV_MAT_ELEM(*rotationsmatrix, float, 0, 1) = 1.2635359098409581e-03;
        CV_MAT_ELEM(*rotationsmatrix, float, 0, 2) = -1.7487233004436643e-02;

        CV_MAT_ELEM(*rotationsmatrix, float, 1, 0) = -1.4779096108364480e-03;
        CV_MAT_ELEM(*rotationsmatrix, float, 1, 1) = 9.9992385683542895e-01;
        CV_MAT_ELEM(*rotationsmatrix, float, 1, 2) = -1.2251380107679535e-02;

        CV_MAT_ELEM(*rotationsmatrix, float, 2, 0) = 1.7470421412464927e-02;
        CV_MAT_ELEM(*rotationsmatrix, float, 2, 1) = 1.2275341476520762e-02;
        CV_MAT_ELEM(*rotationsmatrix, float, 2, 2) = 9.9977202419716948e-01;
    }

    if (!translationsmatrix) {
        translationsmatrix = cvCreateMat(3, 1, CV_32FC1);

        CV_MAT_ELEM(*translationsmatrix, float, 0, 0) = 1.9985242312092553e-02;
        CV_MAT_ELEM(*translationsmatrix, float, 1, 0) = -7.4423738761617583e-04;
        CV_MAT_ELEM(*translationsmatrix, float, 2, 0) = -1.0916736334336222e-02;
    }

    if (!P3D)
        P3D = cvCreateMat(3, 1, CV_32FC1);

    if (!result)
        result = cvCreateMat(3, 1, CV_32FC1);

    int col, row, i;
    for (i = 0; i < (640*480); i++) {
        pushrgb(rgb_masked, i, 250, 250, 0);
    }
    for (row = 0; row < (480); row++) {
        for (col = 0; col < 640; col++) {
            i = row * 640 + col;
            int depth = glow_mid[3 * i + 0];
            if (depth == 0)
                continue;

            /* Koordinaten umrechnen in Koordinaten auf dem RGB-Bild */
            // X
            CV_MAT_ELEM(*P3D, float, 0, 0) = ((col - 3.3930780975300314e+02) * depth) / 5.9421434211923247e+02;
            // Y
            CV_MAT_ELEM(*P3D, float, 1, 0) = ((row - 2.4273913761751615e+02) * depth) / 5.9104053696870778e+02;
            // depth
            CV_MAT_ELEM(*P3D, float, 2, 0) = depth;

            cvMatMul(rotationsmatrix, P3D, result);
            cvAdd(result, translationsmatrix, result, NULL);

            double xb = (CV_MAT_ELEM(*result, float, 0, 0) * 5.2921508098293293e+02 / CV_MAT_ELEM(*result, float, 2, 0)) + 3.2894272028759258e+02;
            double yb = (CV_MAT_ELEM(*result, float, 1, 0) * 5.2556393630057437e+02 / CV_MAT_ELEM(*result, float, 2, 0)) + 2.6748068171871557e+02;

            int di = ((int)yb * 640) + (int)xb;
            if (glow_mid[3 * i + 1] != 255 && glow_mid[3 * i + 1] != 0) {
                pushrgb(rgb_masked, i, rgb_mid[3 * di + 0], rgb_mid[3 * di + 1], rgb_mid[3 * di + 2]);
            } else {
                pushrgb(rgb_masked, i, 255, 0, 0);
            }
        }
    }
#endif
#if 0
    /* RGB-Bild maskieren */
    int col, row, i;
    for (row = 0; row < (480); row++) {
        for (col = 0; col < 640; col++) {
            i = row * 640 + col;

            if (glow_mid[3 * i + 1] != 255 && glow_mid[3 * i + 1] != 0) {
                pushrgb(rgb_masked, i, rgb_mid[3 * i + 0], rgb_mid[3 * i + 1], rgb_mid[3 * i + 2]);
            } else {
                pushrgb(rgb_masked, i, 255, 0, 0);
            }

        }
    }

    /* Buffer austauschen (double-buffering) */
    swap_buffer(rgb);
    swap_buffer(rgb_masked);
}
#endif

bool calibration = false;

static void select_reference_color(int x, int y) {
    printf("Handling click on x = %d, y = %d\n", x, y);
    GLuint left_buffer, right_buffer, buffer;
    kb_images_current_buffers(&left_buffer, &right_buffer);
    printf("left texture id = %d, right = %d\n", left_buffer, right_buffer);
    uchar4 *gpu_buffer = NULL;
    if (x < 640) {
        /* The clicked pixel is in the left image */
        buffer = left_buffer;
    } else {
        /* The clicked pixel is in the right image */
        buffer = right_buffer;
        x -= 640;
    }
    cutilSafeCall(cudaGLMapBufferObject((void**)&gpu_buffer, buffer));
    uchar4 pixel;
    cudaMemcpy(&pixel, gpu_buffer + (y * 640) + x, sizeof(uchar4), cudaMemcpyDeviceToHost);
    printf("pixel-value: %d, %d, %d (%d)\n", pixel.x, pixel.y, pixel.z, pixel.w);
    static char rgbbuffer[4096];
    snprintf(rgbbuffer, sizeof(rgbbuffer), "%d,%d,%d", pixel.z, pixel.y, pixel.x);
    kb_ui_call_javascript("SetRGB", rgbbuffer);
    printf("err: %s\n", gluErrorString(glGetError()));
    cutilSafeCall(cudaGLUnmapBufferObject(buffer));
}

// Callbacks

// Calibration button callback
static void run_calibration_callback(void) {
    calibration = !calibration;
    if (calibration)
        median_clear_calibration();
} 

// Exit Callback
static void exit_callback() {
    kinect_shutdown();
    exit(0);    
}

static void set_distance_threshold_callback(float val) {
    printf("%f", val);
}
static void kb_poll_events(void) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        switch (event.type) {  
            case SDL_MOUSEMOTION:
                kb_ui_inject_mouse(event.motion.x, event.motion.y);
                break;
            case SDL_MOUSEBUTTONUP:
                kb_ui_inject_mouse_button(event.button.button, false);
                break;
            case SDL_MOUSEBUTTONDOWN:
                if (event.button.y < 480) {
                    select_reference_color(event.button.x, event.button.y);
                } else {
                    kb_ui_inject_mouse_button(event.button.button, true);
                }
                break;
            case SDL_KEYDOWN:
                switch (event.key.keysym.sym) {
                    case SDLK_ESCAPE:
                        exit_callback();
                    case SDLK_LEFT:
                        kb_images_scroll_left();
                        break;
                    case SDLK_RIGHT:
                        kb_images_scroll_right();
                        break;
                    case SDLK_e:
                        run_calibration_callback();
                        break;
                    default:
                        printf("Unknown key pressed.\n");
                        break;
                }
                break;
        }
    }
}

/*
 * Allocates a GL buffer and texture to be used on the GPU.
 *
 */
static void allocateGLTexture(GLuint *bufferID, GLuint *textureID) {
    glGenBuffers(1, bufferID);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, *bufferID);
    glBufferData(GL_PIXEL_UNPACK_BUFFER, 640 * 480 * 4 * sizeof(GLubyte), NULL, GL_DYNAMIC_COPY);
    cudaGLRegisterBufferObject(*bufferID);

    glEnable(GL_TEXTURE_2D);
    glGenTextures(1, textureID);
    glBindTexture(GL_TEXTURE_2D, *textureID);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 640, 480, 0, GL_BGRA, GL_UNSIGNED_BYTE, NULL);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
}

int main(int argc, char *argv[]) {
    SDL_Surface *screen;

    median_filter_init();
    glow_filter_init();
    //kinect_init();
    mask_rgb_init();
    
    /* Initialize SDL */
    SDL_Init(SDL_INIT_VIDEO);
    TTF_Init();

    SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 16);
    SDL_GL_SetAttribute(SDL_GL_BUFFER_SIZE, 32);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_SWAP_CONTROL, 1);

    /* Initialize the screen / window */
    screen = SDL_SetVideoMode(SCREEN_WIDTH, SCREEN_HEIGHT, SCREEN_DEPTH, SDL_OPENGL | SDL_HWSURFACE | SDL_NOFRAME | SDL_DOUBLEBUF);
    if (screen == 0) {
        printf("set failed: %s\n", SDL_GetError());
        return 1;
    }
    SDL_WM_SetCaption("kinectboard", "");

    glewInit();

    /* Setup viewport */
    glEnable(GL_TEXTURE_2D);
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glViewport(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
    glClear(GL_COLOR_BUFFER_BIT);

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    gluOrtho2D(0, SCREEN_WIDTH, 0, SCREEN_HEIGHT);

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    
    kb_ui_init();
    
    // Register callbacks
    kb_ui_register_void_callback("Exit",exit_callback);
    kb_ui_register_void_callback("Calibrate",run_calibration_callback);
    kb_ui_register_void_callback("ImageRight",kb_images_scroll_right);
    kb_ui_register_void_callback("ImageLeft",kb_images_scroll_left);
    kb_ui_register_value_callback("SetDistanceThreshold", set_distance_threshold_callback);

    // The CUDA Device Info requires a valid UI since the info is displayed there
    print_cuda_device_info();

    /* Allocate textures and buffers to draw into (from the GPU) */
    allocateGLTexture(&rawDepthBufferID, &rawDepthTextureID);
    allocateGLTexture(&medianBufferID, &medianTextureID);
    allocateGLTexture(&maskedMedianBufferID, &maskedMedianTextureID);
    allocateGLTexture(&glowBufferID, &glowTextureID);
    allocateGLTexture(&rawRgbBufferID, &rawRgbTextureID);
    allocateGLTexture(&maskRgbBufferID, &maskRgbTextureID);

    kb_image_create("Raw depth image", rawDepthBufferID, rawDepthTextureID);
    kb_image_create("Median-filtered depth image", medianBufferID, medianTextureID);
    kb_image_create("Masked depth image", maskedMedianBufferID, maskedMedianTextureID);
    kb_image_create("Glowing depth", glowBufferID, glowTextureID);
    kb_image_create("Raw RGB image", rawRgbBufferID, rawRgbTextureID);
    kb_image_create("Masked kinect RGB image", maskRgbBufferID, maskRgbTextureID);

    printf("gl set up.\n");
 
    uchar4 *gpu_median_output,
           *gpu_masked_median_output,
           *gpu_glow_output,
           *gpu_mask_rgb_output,
           *gpu_raw_depth_output,
           *gpu_raw_rgb_output;

    int fps = 0;
    int last_time = 0;
    int current_time;

    while (1) {
        /* FPS counter */
        current_time = SDL_GetTicks();
        if ((current_time - last_time) >= 1000) {
            static char buffer[20] = {0};
            sprintf(buffer, "%d FPS", fps);
            SDL_WM_SetCaption(buffer, 0);
            kb_ui_call_javascript("SetFPS",buffer);
            fps = 0;
            last_time = current_time;
        }

        //kb_poll_events(list);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

        /* Reset viewport for rendering our images, it was modified by
         * kb_ui_render(). */
        glViewport(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
        glClear(GL_COLOR_BUFFER_BIT);

        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        gluOrtho2D(0, SCREEN_WIDTH, 0, SCREEN_HEIGHT);

        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();

        kb_poll_events();

        gpu_median_output = NULL;
        gpu_masked_median_output = NULL;
        gpu_glow_output = NULL;
        gpu_mask_rgb_output = NULL;
        gpu_raw_depth_output = NULL;
        gpu_raw_rgb_output = NULL;

        cutilSafeCall(cudaGLMapBufferObject((void**)&gpu_raw_depth_output, rawDepthBufferID));
        cutilSafeCall(cudaGLMapBufferObject((void**)&gpu_median_output, medianBufferID));
        cutilSafeCall(cudaGLMapBufferObject((void**)&gpu_masked_median_output, maskedMedianBufferID));
        cutilSafeCall(cudaGLMapBufferObject((void**)&gpu_glow_output, glowBufferID));
        cutilSafeCall(cudaGLMapBufferObject((void**)&gpu_mask_rgb_output, maskRgbBufferID));
        cutilSafeCall(cudaGLMapBufferObject((void**)&gpu_raw_rgb_output, rawRgbBufferID));

        // XXX: Potential for optimization: We currently call functions like
        // median_filter(), median_mask() and mask_rgb() which are all
        // blocking. However, we could launch the kernel and perform more work
        // on the CPU while waiting for the kernel to complete (or maybe even
        // launch some in parallel and/or use async events).

        median_filter(take_depth_image(), gpu_median_output, gpu_raw_depth_output);
        done_depth_image();

        median_mask(calibration, gpu_median_output, gpu_masked_median_output);
        glow_filter(gpu_masked_median_output, gpu_glow_output);

        mask_rgb(gpu_glow_output, take_rgb_image(), gpu_mask_rgb_output, gpu_raw_rgb_output);
        done_rgb_image();

        cutilSafeCall(cudaGLUnmapBufferObject(maskedMedianBufferID));
        cutilSafeCall(cudaGLUnmapBufferObject(medianBufferID));
        cutilSafeCall(cudaGLUnmapBufferObject(glowBufferID));
        cutilSafeCall(cudaGLUnmapBufferObject(maskRgbBufferID));
        cutilSafeCall(cudaGLUnmapBufferObject(rawDepthBufferID));
        cutilSafeCall(cudaGLUnmapBufferObject(rawRgbBufferID));

        kb_images_render();

        kb_ui_update();
        kb_ui_render();

        SDL_GL_SwapBuffers();
        fps++;
    }
}
