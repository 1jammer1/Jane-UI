#include <SDL2/SDL.h>
#include <SDL2/SDL_mixer.h>
#include <microhttpd.h>
#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define SERVER_PORT            8887
#define WINDOW_WIDTH           800
#define WINDOW_HEIGHT          600
#define CIRCLE_MIN_RADIUS      50
#define CIRCLE_MAX_RADIUS      300

#define CAPTURE_FREQ           44100
#define CAPTURE_FORMAT         AUDIO_S16SYS
#define CAPTURE_CHANNELS       1
#define CAPTURE_SAMPLES        1024
#define START_KEY              SDLK_r
#define START_MASK             KMOD_CTRL
#define RECORD_FILE            "recorded.wav"
static const char *POST_URL = "http://127.0.0.1:8888/mic";

static float current_volume = 0.0f;
static SDL_mutex *volume_mutex = NULL;
static Mix_Music *current_music = NULL;

/* For HTTP server upload */
struct RequestData {
    FILE *fp;
};

/* libmicrohttpd callback for POST /upload */
static enum MHD_Result
handle_post(void *cls, struct MHD_Connection *connection,
            const char *url, const char *method,
            const char *version, const char *upload_data,
            size_t *upload_data_size, void **ptr)
{
    struct RequestData *data = *ptr;
    if (strcmp(url, "/upload") != 0 || strcmp(method, "POST") != 0)
        return MHD_NO;

    if (data == NULL) {
        data = malloc(sizeof(*data));
        data->fp = fopen("temp_audio.wav", "wb");
        if (!data->fp) { free(data); return MHD_NO; }
        *ptr = data;
        return MHD_YES;
    }

    if (*upload_data_size > 0) {
        fwrite(upload_data, 1, *upload_data_size, data->fp);
        *upload_data_size = 0;
        return MHD_YES;
    }

    /* all data received */
    fclose(data->fp);
    free(data);
    *ptr = NULL;

    /* reply */
    struct MHD_Response *resp = MHD_create_response_from_buffer(
        strlen("File uploaded"), "File uploaded", MHD_RESPMEM_PERSISTENT);
    MHD_queue_response(connection, MHD_HTTP_OK, resp);
    MHD_destroy_response(resp);

    /* signal SDL to play it */
    SDL_Event ev;
    ev.type = SDL_USEREVENT;
    ev.user.code = 1;
    SDL_PushEvent(&ev);
    return MHD_YES;
}

/* SDL_mixer post-mix to compute RMS of what's playing */
void audio_playback_callback(void *unused, Uint8 *stream, int len) {
    (void)unused;
    Sint16 *samples = (Sint16*)stream;
    int count = len / sizeof(Sint16);
    double sum = 0;
    for (int i = 0; i < count; i++)
        sum += samples[i] * (double)samples[i];
    double rms = sqrt(sum / count);
    float vol = (float)(rms / 32768.0);
    SDL_LockMutex(volume_mutex);
    current_volume = vol;
    SDL_UnlockMutex(volume_mutex);
}

/* Raw capture buffer */
static Uint8  *pcm_buffer   = NULL;
static size_t  pcm_capacity = 0;
static size_t  pcm_length   = 0;
static int     recording    = 0;
static SDL_AudioDeviceID capture_dev = 0;

/* SDL capture callback */
void capture_callback(void *ud, Uint8 *stream, int len) {
    (void)ud;
    if (!recording) return;
    if (pcm_length + len > pcm_capacity) {
        size_t grow = CAPTURE_SAMPLES * sizeof(int16_t) * 10;
        Uint8 *nb = realloc(pcm_buffer, pcm_capacity + grow);
        if (!nb) { recording = 0; return; }
        pcm_buffer = nb;
        pcm_capacity += grow;
    }
    memcpy(pcm_buffer + pcm_length, stream, len);
    pcm_length += len;
}

/* Write WAV header + PCM */
void write_wav(const char *fn) {
    FILE *f = fopen(fn, "wb");
    if (!f) return;
    uint32_t datalen = pcm_length;
    uint16_t bits = SDL_AUDIO_BITSIZE(CAPTURE_FORMAT);
    uint16_t block_align = CAPTURE_CHANNELS * (bits/8);
    uint32_t byte_rate = CAPTURE_FREQ * block_align;
    uint32_t chunksize = 36 + datalen;

    fwrite("RIFF",1,4,f); fwrite(&chunksize,4,1,f); fwrite("WAVE",1,4,f);
    fwrite("fmt ",1,4,f);
    uint32_t sub1 = 16; fwrite(&sub1,4,1,f);
    uint16_t fmt = 1; fwrite(&fmt,2,1,f);
    uint16_t channels = CAPTURE_CHANNELS;
    uint32_t freq = CAPTURE_FREQ;
    fwrite(&channels, 2, 1, f);
    fwrite(&freq,    4, 1, f);
    fwrite(&byte_rate,4,1,f);
    fwrite(&block_align,2,1,f);
    fwrite(&bits,2,1,f);
    fwrite("data",1,4,f); fwrite(&datalen,4,1,f);
    fwrite(pcm_buffer,1,pcm_length,f);
    fclose(f);
}

/* POST via libcurl */
void post_back(const char *url) {
    CURL *c = curl_easy_init();
    if (!c) return;
    curl_mime *m = curl_mime_init(c);
    curl_mimepart *p = curl_mime_addpart(m);
    curl_mime_name(p, "file");
    curl_mime_filedata(p, RECORD_FILE);
    curl_easy_setopt(c, CURLOPT_URL, url);
    curl_easy_setopt(c, CURLOPT_MIMEPOST, m);
    curl_easy_perform(c);
    curl_mime_free(m);
    curl_easy_cleanup(c);
}

int main(int argc, char *argv[]) {
    if (SDL_Init(SDL_INIT_VIDEO|SDL_INIT_AUDIO) < 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }

    SDL_Window   *win      = SDL_CreateWindow(
        "Recorder+Visualizer", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        WINDOW_WIDTH, WINDOW_HEIGHT, SDL_WINDOW_SHOWN);
    SDL_Renderer *ren      = SDL_CreateRenderer(win,-1,SDL_RENDERER_ACCELERATED);

    /* Init SDL_mixer for playback */
    if (Mix_OpenAudio(44100, AUDIO_S16SYS, 2, 1024) < 0) {
        fprintf(stderr,"Mix_OpenAudio: %s\n",Mix_GetError());
        return 1;
    }
    Mix_SetPostMix(audio_playback_callback, NULL);

    volume_mutex = SDL_CreateMutex();

    /* Start HTTP server on SERVER_PORT */
    struct MHD_Daemon *daemon = MHD_start_daemon(
        MHD_USE_THREAD_PER_CONNECTION,
        SERVER_PORT,
        NULL, NULL,
        &handle_post, NULL,
        MHD_OPTION_END);
    if (!daemon) {
        fprintf(stderr, "MHD_start_daemon failed\n");
        return 1;
    }

    /* Set up capture device */
    SDL_AudioSpec want;
    SDL_zero(want);
    want.freq     = CAPTURE_FREQ;
    want.format   = CAPTURE_FORMAT;
    want.channels = CAPTURE_CHANNELS;
    want.samples  = CAPTURE_SAMPLES;
    want.callback = capture_callback;
    capture_dev = SDL_OpenAudioDevice(NULL, SDL_TRUE, &want, NULL, 0);
    SDL_PauseAudioDevice(capture_dev, 1);  // paused

    /* Main loop */
    SDL_Event e;
    int quit = 0;
    while (!quit) {
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) {
                quit = 1;
            }
            else if (e.type == SDL_USEREVENT && e.user.code == 1) {
                /* play the uploaded temp_audio.wav */
                if (current_music) {
                    Mix_HaltMusic();
                    Mix_FreeMusic(current_music);
                }
                current_music = Mix_LoadMUS("temp_audio.wav");
                if (current_music)
                    Mix_PlayMusic(current_music,1);
            }
            else if (e.type == SDL_KEYDOWN &&
                     e.key.keysym.sym == START_KEY &&
                     (e.key.keysym.mod & START_MASK) &&
                     !recording)
            {
                pcm_length = 0;
                recording  = 1;
                SDL_PauseAudioDevice(capture_dev, 0);
                printf("Recording started...\n");
            }
            else if (e.type == SDL_KEYUP &&
                     e.key.keysym.sym == START_KEY &&
                     recording)
            {
                recording = 0;
                SDL_PauseAudioDevice(capture_dev, 1);
                printf("Recording stopped (%zu bytes)\n", pcm_length);
                write_wav(RECORD_FILE);
                post_back(POST_URL);
                printf("Posted %s to %s\n", RECORD_FILE, POST_URL);
            }
        }

        /* render */
        SDL_SetRenderDrawColor(ren,0,0,0,255);
        SDL_RenderClear(ren);
        SDL_LockMutex(volume_mutex);
        float radius = CIRCLE_MIN_RADIUS
                     + (CIRCLE_MAX_RADIUS - CIRCLE_MIN_RADIUS) * current_volume;
        SDL_UnlockMutex(volume_mutex);
        SDL_SetRenderDrawColor(ren,255,255,255,255);
        int cx = WINDOW_WIDTH/2, cy = WINDOW_HEIGHT/2;
        for (int w=0; w<radius*2; w++) {
            for (int h=0; h<radius*2; h++){
                int dx = w - radius, dy = h - radius;
                if (dx*dx + dy*dy <= radius*radius)
                    SDL_RenderDrawPoint(ren, cx+dx, cy+dy);
            }
        }
        SDL_RenderPresent(ren);
        SDL_Delay(16);
    }

    /* cleanup */
    MHD_stop_daemon(daemon);
    SDL_CloseAudioDevice(capture_dev);
    SDL_DestroyMutex(volume_mutex);
    Mix_FreeMusic(current_music);
    Mix_CloseAudio();
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    free(pcm_buffer);
    return 0;
}
