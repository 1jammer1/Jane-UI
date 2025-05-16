#include <SDL2/SDL.h>
#include <SDL2/SDL_mixer.h>
#include <microhttpd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define PORT 8888
#define WINDOW_WIDTH 800
#define WINDOW_HEIGHT 600
#define CIRCLE_MIN_RADIUS 50
#define CIRCLE_MAX_RADIUS 300

static float current_volume = 0.0f;
static SDL_mutex *volume_mutex = NULL;
static Mix_Music *current_music = NULL;

struct RequestData {
    FILE *fp;
};

static enum MHD_Result handle_post(void *cls, struct MHD_Connection *connection,
                                  const char *url, const char *method,
                                  const char *version, const char *upload_data,
                                  size_t *upload_data_size, void **ptr) {
    struct RequestData *data = *ptr;
    if (strcmp(url, "/upload") != 0)
        return MHD_NO;
    if (0 != strcmp(method, "POST"))
        return MHD_NO;
    if (data == NULL) {
        data = malloc(sizeof(struct RequestData));
        data->fp = fopen("temp_audio.wav", "wb");
        if (!data->fp) {
            free(data);
            return MHD_NO;
        }
        *ptr = data;
        return MHD_YES;
    } else if (*upload_data_size != 0) {
        fwrite(upload_data, 1, *upload_data_size, data->fp);
        *upload_data_size = 0;
        return MHD_YES;
    } else {
        fclose(data->fp);
        free(data);
        *ptr = NULL;

        struct MHD_Response *response = MHD_create_response_from_buffer(
            strlen("File uploaded successfully"), "File uploaded successfully", MHD_RESPMEM_PERSISTENT);
        enum MHD_Result ret = MHD_queue_response(connection, MHD_HTTP_OK, response);
        MHD_destroy_response(response);

        SDL_Event event;
        event.type = SDL_USEREVENT;
        event.user.code = 1;
        SDL_PushEvent(&event);

        return ret;
    }
}

void audio_callback(void *userdata, Uint8 *stream, int len) {
    Sint16 *samples = (Sint16 *)stream;
    int num_samples = len / sizeof(Sint16);
    double sum = 0.0;
    for (int i = 0; i < num_samples; i++) {
        sum += samples[i] * samples[i];
    }
    double rms = sqrt(sum / num_samples);
    float volume = (float)(rms / 32768.0f);

    SDL_LockMutex(volume_mutex);
    current_volume = volume;
    SDL_UnlockMutex(volume_mutex);
}

int main(int argc, char *argv[]) {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) < 0) {
        printf("SDL could not initialize! SDL_Error: %s\n", SDL_GetError());
        return 1;
    }

    SDL_Window *window = SDL_CreateWindow("Audio Visualizer", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                          WINDOW_WIDTH, WINDOW_HEIGHT, SDL_WINDOW_SHOWN);
    if (!window) {
        printf("Window could not be created! SDL_Error: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    SDL_Renderer *renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    if (!renderer) {
        printf("Renderer could not be created! SDL_Error: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    if (Mix_OpenAudio(44100, AUDIO_S16SYS, 2, 1024) < 0) {
        printf("SDL_mixer could not initialize! Mix_Error: %s\n", Mix_GetError());
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    Mix_SetPostMix(audio_callback, NULL);

    struct MHD_Daemon *daemon = MHD_start_daemon(MHD_USE_THREAD_PER_CONNECTION, PORT, NULL, NULL,
                                                 &handle_post, NULL, MHD_OPTION_END);
    if (!daemon) {
        printf("Failed to start HTTP server\n");
        Mix_CloseAudio();
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    volume_mutex = SDL_CreateMutex();

    int quit = 0;
    SDL_Event e;

    while (!quit) {
        while (SDL_PollEvent(&e) != 0) {
            if (e.type == SDL_QUIT) {
                quit = 1;
            } else if (e.type == SDL_USEREVENT && e.user.code == 1) {
                if (current_music) {
                    Mix_HaltMusic();
                    Mix_FreeMusic(current_music);
                }
                current_music = Mix_LoadMUS("temp_audio.wav");
                if (!current_music) {
                    printf("Failed to load music: %s\n", Mix_GetError());
                } else {
                    Mix_PlayMusic(current_music, 1);
                }
            }
        }

        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);

        SDL_LockMutex(volume_mutex);
        float radius = CIRCLE_MIN_RADIUS + (CIRCLE_MAX_RADIUS - CIRCLE_MIN_RADIUS) * current_volume;
        SDL_UnlockMutex(volume_mutex);

        SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
        int center_x = WINDOW_WIDTH / 2;
        int center_y = WINDOW_HEIGHT / 2;
        for (int w = 0; w < radius * 2; w++) {
            for (int h = 0; h < radius * 2; h++) {
                int dx = w - radius;
                int dy = h - radius;
                if (dx * dx + dy * dy <= radius * radius) {
                    SDL_RenderDrawPoint(renderer, center_x + dx, center_y + dy);
                }
            }
        }

        SDL_RenderPresent(renderer);
    }

    MHD_stop_daemon(daemon);
    SDL_DestroyMutex(volume_mutex);
    Mix_FreeMusic(current_music);
    Mix_CloseAudio();
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}
