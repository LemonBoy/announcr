// announcr
// 2013 (C) The Lemon Man

#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <assert.h>
#include <mpd/client.h>
#include <espeak/speak_lib.h>
#include <ao/ao.h>

static ao_device * dev = NULL;
static struct mpd_connection * conn = NULL;
static int announcr_run = 1;

int synth (short *samples, int samples_count, espeak_EVENT *events)
{
    ao_play(dev, (char *)samples, samples_count << 1);
    return 0;
}

void announce_track (const char *title, const char *artist)
{
    static char announce_buf[1024];

    snprintf((char *)&announce_buf, sizeof(announce_buf), "%s by %s", 
            (title) ? title : "Unknown track", 
            (artist) ? artist : "Unknown artist");

    espeak_Synth(&announce_buf, strlen(announce_buf), 0, 0, 0, espeakCHARS_AUTO, NULL, NULL);
    espeak_Synchronize();
}

int mpd_get_state (struct mpd_connection * c)
{
    struct mpd_status * stat;
    stat = mpd_run_status(c);
    int ret = (stat) ? mpd_status_get_state(stat) : MPD_STATE_UNKNOWN;
    mpd_status_free(stat);
    return ret;
}

void sig_handler (int sig)
{
    announcr_run = 0;
    // Stop polling
    mpd_send_noidle(conn);
}

int main () 
{
    int sample_rate;

    assert((sample_rate = espeak_Initialize(AUDIO_OUTPUT_PLAYBACK, 1500, NULL, 0)) >= 0);
    espeak_SetSynthCallback(&synth);
    espeak_SetVoiceByName("default");

    ao_initialize();

    ao_sample_format fmt;
    memset(&fmt, 0, sizeof(fmt));
    fmt.bits = 16;
    fmt.rate = sample_rate;
    fmt.channels = 1;
    fmt.byte_format = AO_FMT_NATIVE;

    dev = ao_open_live(ao_default_driver_id(), &fmt, NULL);

    conn = mpd_connection_new("localhost", 6600, 11000);
    assert(conn != NULL);

    signal(SIGTERM, sig_handler);
    signal(SIGINT, sig_handler);

    int last_id = -1;
    int last_state = mpd_get_state(conn);
    while (announcr_run) {
        enum mpd_idle idle = mpd_run_idle_mask(conn, MPD_IDLE_PLAYER);

        if (idle) { 
            int state = mpd_get_state(conn);
            // Announce only if going from PAUSE/STOP -> PLAY
            if (state == MPD_STATE_PLAY && 
                    (last_state == MPD_STATE_PAUSE || last_state == MPD_STATE_STOP)) {
                struct mpd_song * song = mpd_run_current_song(conn);
                assert(song != NULL);
                int this_id = mpd_song_get_id(song);
                // Avoid announcing the same song
                if (this_id != last_id) {
                    announce_track(
                            mpd_song_get_tag(song, MPD_TAG_TITLE, 0),
                            mpd_song_get_tag(song, MPD_TAG_ARTIST, 0));
                    last_id = this_id;
                }
                mpd_song_free(song);
            } else {
                last_state = state;
            }
        }
    }

    espeak_Terminate();

    mpd_connection_free(conn);

    ao_close(dev);
    ao_shutdown();
}
