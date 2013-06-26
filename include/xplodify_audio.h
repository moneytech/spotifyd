#ifndef _XPLODIFY_AUDIO_H
#define _XPLODIFY_AUDIO_H

#include <queue>
#include <cstdint>

#include "lockable.h"
#include "runnable.h"

extern "C" {
    #include <OpenAL/al.h>
    #include <OpenAL/alc.h>
}

struct audio_data {
    public:
        uint32_t channels;
        uint32_t rate;
        uint32_t n_samples;
        std::vector<int16_t> samples;

        uint32_t add_samples(const int16_t * smpl, uint32_t n) {
            samples.assign(smpl, smpl+n );
            return n;
        }
};

//This will probably end up being a superclass
class XplodifyAudio 
         : public Runnable
         , private Lockable {

    public:
        XplodifyAudio();
        void flush_queue();
        void enqueue_samples(boost::shared_ptr<audio_data> d);
        boost::shared_ptr<audio_data> get_samples();
    protected:
        void queue_buffer(ALuint src, ALuint buffer);
        // implemeting runnable
        void run();
    private:
        ALCdevice *device = NULL;
        ALCcontext *context = NULL;

        enum { NUM_BUFFERS = 3 };
        ALuint buffers[NUM_BUFFERS];
        ALuint source;
        ALint processed;
        ALenum error;
        ALint rate;
        ALint channels;

        uint32_t qlen;
        std::queue<boost::shared_ptr<audio_data> > audio_queue;
};

#endif //_XPLODIFY_AUDIO_HH
