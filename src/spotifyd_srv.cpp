// This autogenerated skeleton file illustrates how to build a server.
// You should copy it to another filename to avoid overwriting it.

#include <protocol/TBinaryProtocol.h>
#include <server/TSimpleServer.h>
#include <transport/TServerSocket.h>
#include <transport/TBufferTransports.h>

//Boost.
#include <boost/algorithm/string.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/filesystem.hpp>

#include <iostream>
#include <cstring>
#include <string>

#include <sys/types.h>
#include <unistd.h>

#include "Spotify.h"

#include "xplodify_sess.h"
#include "xplodify_pl.h"
#include "spotify_cust.h"


using namespace ::apache::thrift;
using namespace ::apache::thrift::protocol;
using namespace ::apache::thrift::transport;
using namespace ::apache::thrift::server;

//define the operators
//these must be moved to their own file with other possible
//definitions for thrift generated code.
bool SpotifyTrack::operator < (const SpotifyTrack & other) const 
{
    return (this->_id - other._id) < 0;
}

bool SpotifyCredential::operator < (const SpotifyCredential & other) const 
{
    return this->_username.compare(other._username) < 0;
}


XplodifyHandler::XplodifyHandler()
    : Runnable()
    , Lockable()
    , m_sess_it(m_session_cache.get<0>().begin())
    , m_active_session()
    , m_playback_done(1)
    , m_notify_events(0)
    , m_ts(std::time(NULL))
{
    //tmp dir name
    int pid = getpid();
    std::stringstream sst;
    sst << SP_TMPDIR << "-" << pid;
    m_sp_tmpdir = sst.str();

    enum audio_arch arch;
#ifdef _OSX
#ifdef HAS_OPENAL
    arch = OPENAL_ARCH;
#elif HAS_AUDIOTOOLKIT
    arch = AUDIOTOOLKIT;
#endif
#else
#ifdef _LINUX
    arch = ALSA;
#endif
#endif
    set_audio(arch);
    audio_init(&m_audiofifo);

}


void XplodifyHandler::run() 
{
    int next_timeout = 0;

    while(!m_done)
    {
        if (next_timeout == 0) {
            while(!m_notify_events && !m_playback_done) {
                cond_wait();
            }
        } else {
            struct timespec ts;

#if _POSIX_TIMERS > 0
            clock_gettime(CLOCK_REALTIME, &ts);
#else
            struct timeval tv;
            gettimeofday(&tv, NULL);
            TIMEVAL_TO_TIMESPEC(&tv, &ts);
#endif
            ts.tv_sec += next_timeout / 1000;
            ts.tv_nsec += (next_timeout % 1000) * 1000000;
            
            cond_timedwait(&ts);
        }

        m_notify_events = 0;
        unlock();

        if (m_playback_done) {
            //track_ended();
            m_playback_done = 0;
        }

        do {
            //protecting iterator
            lock();
            sess_map_sequenced::iterator sit = m_session_cache.get<0>().begin();
            for( ; sit != m_session_cache.get<0>().end() ; sit++ ) {
                //should we make sure next_timeout is 0?
                sp_session_process_events(sit->session->get_session(), &next_timeout);
            }
            unlock();
        } while (next_timeout == 0);

        m_io.poll();
        m_io.reset();

        lock();
    }
}


void XplodifyHandler::loginSession(SpotifyCredential& _return, const SpotifyCredential& cred) {

    sp_error err;
#ifdef _DEBUG
    printf("initiatingSession\n");
#endif

    boost::shared_ptr< XplodifySession > sess = get_session(cred._uuid);
    if(!sess) {
        sess = XplodifySession::create(this);
        if(sess->init_session(g_appkey, g_appkey_size )) {
            //can't continue....
#ifdef _DEBUG
            std::cout << "Unexpected error creating session. "<< std::endl;
#endif
            sess.reset();
            return;
        }

        sess->login(cred._username, cred._passwd);


        lock();
        //generate UUID
        const boost::uuids::uuid uuid = boost::uuids::random_generator()();
        const std::string uuid_str = boost::lexical_cast<std::string>(uuid);

        sess->set_uuid(uuid_str);
        m_session_cache.get<1>().insert(sess_map_entry( uuid_str, 
                    const_cast<const sp_session *>(sess->get_session()), sess ));
#ifdef _DEBUG
        std::cout << "starting timer for session: "<< uuid_str <<"\n";
#endif
        //no transfer of ownership, we're good with raw pointers.
        boost::asio::deadline_timer * t = new boost::asio::deadline_timer(m_io);
        t->expires_from_now(boost::posix_time::seconds(LOGIN_TO));
        t->async_wait(boost::bind(&XplodifyHandler::login_timeout,
                    this, boost::asio::placeholders::error, uuid_str));

        m_timers.insert(std::pair< std::string, boost::asio::deadline_timer *>(uuid_str, t));

        if(!getActiveSession()) {
            setActiveSession(sess);
        }

        _return = cred;
        _return.__set__uuid(uuid_str);
        unlock();
    }
}
void XplodifyHandler::login_timeout(const boost::system::error_code&,
        std::string uuid) {
#ifdef _DEBUG
    std::cout << "Event timeout for login: " << uuid << " Checking status...\n";
#endif
    timer_map::iterator it = m_timers.find(uuid);

    //Free up resources, cleanup.
    if(it != m_timers.end()) {
        lock();
        delete it->second;
        m_timers.erase(it);
        unlock();
    } else {
        return;
    }

    //check session status...
    boost::shared_ptr< XplodifySession > sess = get_session(uuid);
    if(!sess) {
        return;
    }

    if(sess->get_logged_in()) {
#ifdef _DEBUG
        std::cout << "Session: " << uuid << " Succesfully logged in ...\n";
#endif
        return;
    }

    //not logged in, cleanup.
    lock();

    remove_from_cache(uuid);

    sess->flush();
    sess.reset();
    unlock();
}

bool XplodifyHandler::isLoggedIn(const SpotifyCredential& cred) {

    boost::shared_ptr< XplodifySession > 
        sess = get_session(cred._uuid);

    if(!sess)
        return false;

    return sess->get_logged_in();
}

int64_t XplodifyHandler::getStateTS(const SpotifyCredential& cred) {
    int64_t ts;
    boost::shared_ptr< XplodifySession > 
        sess = get_session(cred._uuid);

    if(!sess)
        return false;

    lock();
    ts = m_ts;
    unlock();

    return ts;
}


void XplodifyHandler::logoutSession(const SpotifyCredential& cred) {

    sp_error err;
    bool switched = false;

    boost::shared_ptr<XplodifySession> sess = get_session(cred._uuid);
    if(!sess) {
        return;
    }

    lock();
    if(sess == m_active_session) 
    {
        //stop track move onto next session.
        switch_session();
        switched = true;

    }

    err = sp_session_logout(sess->get_session());

    if(err == SP_ERROR_OK )
    {
        remove_from_cache(cred._uuid);
    }
    sess->flush();
    sess.reset();
    unlock();

    return;
}

void XplodifyHandler::sendCommand(const SpotifyCredential& cred, const SpotifyCmd::type cmd) {
    std::cout << "sendCommand" << std::endl;

    boost::shared_ptr<XplodifySession> sess = get_session(cred._uuid);
    if(!sess) {
        return;
    }

    switch(cmd){
        case SpotifyCmd::PLAY:
            sess->start_playback();
            break;
        case SpotifyCmd::PAUSE:
            sess->stop_playback();
            audio_fifo_flush_now();
            audio_fifo_set_reset(audio_fifo(), 1);
            break;
        case SpotifyCmd::NEXT:
        case SpotifyCmd::PREV:
            sess->end_of_track();
            break;
        case SpotifyCmd::RAND:
        case SpotifyCmd::LINEAR:
        case SpotifyCmd::REPEAT_ONE:
        case SpotifyCmd::REPEAT:
            sess->set_mode(cmd);
            break;
        default:
            break;
    }
    return;
}

//change session, allow it to be played.
void XplodifyHandler::switch_session() {
    sp_session_player_unload(m_active_session->get_session());

    //Currently just round-robin.
    m_sess_it++;
    if (m_sess_it == m_session_cache.get<0>().end())
    {
        m_sess_it = m_session_cache.get<0>().begin();
    }

    m_active_session = m_sess_it->session;
    //should load track...
    //sp_session_player_load(m_active_session->get_session(), sometrack );

    return;
}


void XplodifyHandler::search(SpotifyPlaylist& _return, const SpotifyCredential& cred,
		const SpotifySearch& criteria) {
    // Your implementation goes here
    printf("search\n");
    boost::shared_ptr<XplodifySession> sess = get_session(cred._uuid);
    if(!sess) {
        return;
    }
}

void XplodifyHandler::getPlaylists(SpotifyPlaylistList& _return, const SpotifyCredential& cred) 
{
    boost::shared_ptr<XplodifySession> sess = get_session(cred._uuid);
    if(!sess) {
        return;
    }
    boost::shared_ptr<XplodifyPlaylistContainer> pc = sess->get_pl_container();
    if(!pc) {
        return;
    }

    int n =  pc->get_num_playlists();
#ifdef _DEBUG
    std::cout << "About to retrieve " << n << "playlists.";
#endif
    for (int i = 0; i<n; ++i)
    {
        boost::shared_ptr<XplodifyPlaylist> pl = pc->get_playlist(i);
        std::string plstr(pl->get_name());
        _return.insert(plstr);
    }

    return;

}

void XplodifyHandler::getPlaylist(SpotifyPlaylist& _return, const SpotifyCredential& cred,
        const int32_t plist_id) {
    // Your implementation goes here
    boost::shared_ptr<XplodifySession> sess = get_session(cred._uuid);
    if(!sess) {
        return;
    }

    boost::shared_ptr<XplodifyPlaylistContainer> pc = sess->get_pl_container();
    if(!pc) {
        return;
    }

    boost::shared_ptr<XplodifyPlaylist> pl = pc->get_playlist(plist_id);

    if(!pl) {
        return;
    }

    for(unsigned int j = 0 ; j < pl->get_num_tracks() ; j++ ) {
        boost::shared_ptr<XplodifyTrack> tr = pl->get_track_at(j);
#ifdef _DEBUG
        if(!tr->is_loaded()) {
            std::cout << "Track at index: "<<  j << " is loading" << std::endl;
        }
#endif
        int duration = tr->get_duration(); //millisecs?
        SpotifyTrack spt;

        spt.__set__id( j );
        spt.__set__name( tr->get_name() );
        spt.__set__artist( tr->get_artist(0) ); //first artist (this sucks).
        spt.__set__minutes( duration / 60000 );
        spt.__set__seconds( (duration / 1000) % 60 );
        spt.__set__popularity( tr->get_popularity() );
        spt.__set__starred( tr->is_starred() );
        spt.__set__genre( "unknown" );
        _return.push_back(spt);
    }

    return;
}

void XplodifyHandler::getPlaylistByName(
        SpotifyPlaylist& _return, const SpotifyCredential& cred,
        const std::string& name) {

#ifdef _DEBUG
    std::cout << "getPlaylistByName: " << name << "\n";
#endif

    boost::shared_ptr<XplodifySession> sess = get_session(cred._uuid);
    if(!sess) {
        return;
    }


    boost::shared_ptr<XplodifyPlaylistContainer> pc = sess->get_pl_container();
    if(!pc) {
        return;
    }

    boost::shared_ptr<XplodifyPlaylist> pl = pc->get_playlist(name);
    if(!pl) {
        return;
    }

    for(unsigned int j = 0 ; j < pl->get_num_tracks() ; j++ ) {
        boost::shared_ptr<XplodifyTrack> tr = pl->get_track_at(j);
        if(tr->is_loaded()) {
            int duration = tr->get_duration(); //millisecs?
            //boost::shared_ptr<SpotifyTrack> spt(new SpotifyTrack());
            SpotifyTrack spt;

            spt.__set__id( j );
            spt.__set__name( tr->get_name() );
            spt.__set__artist( tr->get_artist(0) ); //first artist (this sucks).
            spt.__set__minutes( duration / 60000 );
            spt.__set__seconds( (duration / 1000) % 60 );
            spt.__set__popularity( tr->get_popularity() );
            spt.__set__starred( tr->is_starred() );
            spt.__set__genre( "unknown" );
            _return.push_back(spt);
        }
#ifdef _DEBUG
        else {
            std::cout << "Track at index:  "<<  j << " is loading" << std::endl;
        }
#endif
    }
#ifdef _DEBUG
    SpotifyPlaylist::const_iterator it;
    for(it = _return.begin() ; it != _return.end() ; it++) {
        std::cout << (*it)._name << "\n";
    }
#endif

    return;
}

void XplodifyHandler::selectPlaylist(const SpotifyCredential& cred, const std::string& playlist) {

    boost::shared_ptr<XplodifySession> sess = get_session(cred._uuid);
    if(!sess) {
        return;
    }

    sess->set_active_playlist(playlist);

}
void XplodifyHandler::selectPlaylistById(const SpotifyCredential& cred, const int32_t plist_id) {
    // Your implementation goes here
    printf("selectPlaylist\n");
    boost::shared_ptr< XplodifySession > sess = get_session(cred._uuid);
    if(!sess) {
	return;
    }

    sess->set_active_playlist(plist_id);
}

void XplodifyHandler::selectTrack(const SpotifyCredential& cred, const std::string& track) {

    //TODO
    boost::shared_ptr< XplodifySession > sess = get_session(cred._uuid);
    if(!sess) {
        return;
    }

#ifdef _DEBUG
    std::cout << "Selecting track " << track << " for session with uuid: " 
        << sess->m_uuid << std::endl;
#endif
    sess->set_track(track);
    return;

}
void XplodifyHandler::selectTrackById(const SpotifyCredential& cred, const int32_t track_id) {

    //TODO
    boost::shared_ptr< XplodifySession > sess = get_session(cred._uuid);
    if(!sess) {
        return;
    }

    sess->set_track(track_id);
    return;
}

bool XplodifyHandler::merge2playlist(const SpotifyCredential& cred, const std::string& pl,
		const SpotifyPlaylist& tracks) {
    boost::shared_ptr< XplodifySession > sess = get_session(cred._uuid);
    if(!sess) {
        return false;
    }
    // Your implementation goes here
    printf("merge2playlist\n");
    return true;
}

bool XplodifyHandler::add2playlist(const SpotifyCredential& cred, const std::string& pl,
		const SpotifyTrack& track) {
    boost::shared_ptr< XplodifySession > sess = get_session(cred._uuid);
    if(!sess) {
        return false;
    }
    // Your implementation goes here
    printf("add2playlist\n");
    return true;
}

void XplodifyHandler::whats_playing(SpotifyTrack& _return) {
    // Your implementation goes here
    printf("whats_playing\n");
}

boost::shared_ptr<XplodifySession> XplodifyHandler::get_session(const std::string& uuid) {

    sess_map_by_uuid& sess_by_uuid = m_session_cache.get<1>();

    sess_map_by_uuid::iterator sit = sess_by_uuid.find(uuid);
    if( sit == m_session_cache.get<1>().end() ) {
        return boost::shared_ptr<XplodifySession>();
    }

    return sit->session;
}

boost::shared_ptr<XplodifySession> XplodifyHandler::get_session(const sp_session * sps) {

    sess_map_by_sessptr& sessByPtr = m_session_cache.get<2>();

    sess_map_by_sessptr::iterator sit = sessByPtr.find(reinterpret_cast<uintptr_t>(sps));
    if( sit == m_session_cache.get<2>().end() ) {
        return boost::shared_ptr<XplodifySession>();
    }

    return sit->session;
}

boost::shared_ptr<XplodifySession> XplodifyHandler::getActiveSession(void) {
    return m_active_session;
}
void XplodifyHandler::setActiveSession(boost::shared_ptr<XplodifySession> session) {
    m_active_session = session;
}

//we also need to be able to search by sp_session, that's quite important; callbacks rely very heavily
//on it.
sp_playlistcontainer * XplodifyHandler::getPlaylistContainer(SpotifyCredential& cred) {
    boost::shared_ptr<XplodifySession> sess = get_session(cred._uuid);
    if(!sess) {
	return NULL;
    }

    return sp_session_playlistcontainer(sess->get_session());
}


audio_fifo_t * XplodifyHandler::audio_fifo() {
    return &m_audiofifo;
}

void XplodifyHandler::set_playback_done(bool done)
{
    lock();
    m_playback_done = done;
    unlock();
}

void XplodifyHandler::notify_main_thread(void)
{
    lock();
    m_notify_events = 1;
    cond_signal();
    unlock();
}

int XplodifyHandler::music_playback(const sp_audioformat *format,
	const void *frames, int num_frames)
{
    size_t s;
    audio_fifo_data_t *afd;

    if (num_frames == 0)
    {
        return 0; // Audio discontinuity, do nothing
    }

    //we're receiving synthetic "end of track" silence...
    if (num_frames > SILENCE_N_SAMPLES) {
        m_active_session->end_of_track();
        pthread_mutex_unlock(&m_audiofifo.mutex);
        return 0;
    }

    pthread_mutex_lock(&m_audiofifo.mutex);

    /* Buffer one second of audio, no more */
    if (m_audiofifo.qlen > format->sample_rate)
    {
#ifdef _DEBUG
        std::cout << "[INFO] Frames in audio_queue: " << m_audiofifo.qlen << std::endl;
#endif
        pthread_mutex_unlock(&m_audiofifo.mutex);
        return 0;
    } 

    //buffer underrun
    if( m_audiofifo.prev_qlen && !m_audiofifo.qlen) 
    {
        m_audiofifo.reset = 1;
#ifdef _DEBUG
        std::cout << "[WARNING] Buffer underrun detected." << std::endl;
#endif
    }

    s = num_frames * sizeof(int16_t) * format->channels;

    //dont want to malloc, change this to new....
    afd = (audio_fifo_data_t *) malloc(sizeof(audio_fifo_data_t) + s);
    memcpy(afd->samples, frames, s);

    afd->nsamples = num_frames;
    afd->rate = format->sample_rate;
    afd->channels = format->channels;

    TAILQ_INSERT_TAIL(&m_audiofifo.q, afd, link);
    m_audiofifo.prev_qlen = m_audiofifo.qlen;
    m_audiofifo.qlen += num_frames;

#ifdef _DEBUG
    std::cout << "[INFO] Frames fed: " << num_frames << std::endl;
    std::cout << "[INFO] Frames in audio_queue: " << m_audiofifo.qlen << std::endl;
#endif

    pthread_cond_signal(&m_audiofifo.cond);
    pthread_mutex_unlock(&m_audiofifo.mutex);

    return num_frames;
}

void XplodifyHandler::audio_fifo_stats(sp_audio_buffer_stats *stats)
{

    pthread_mutex_lock(&m_audiofifo.mutex);

    stats->samples = m_audiofifo.qlen;
    stats->stutter = 0; //how do we calculate this?

    pthread_cond_signal(&m_audiofifo.cond);
    pthread_mutex_unlock(&m_audiofifo.mutex);

}

void XplodifyHandler::audio_fifo_flush_now() {
    audio_fifo_flush(audio_fifo());
}

void XplodifyHandler::update_timestamp() {
    lock();
    m_ts = std::time(NULL);
    unlock();
}

std::string XplodifyHandler::get_tmpdir() {
    return m_sp_tmpdir;
}

int main(int argc, char **argv) {
    int port = 9090;
    enum audio_arch arch;

    //XplodifyHandler
    boost::shared_ptr<XplodifyHandler> sHandler(new XplodifyHandler());

    //THRIFT Server
    boost::shared_ptr<TProcessor> processor(new SpotifyProcessor(sHandler));
    boost::shared_ptr<TServerTransport> serverTransport(new TServerSocket(port));
    boost::shared_ptr<TTransportFactory> transportFactory(new TBufferedTransportFactory());
    boost::shared_ptr<TProtocolFactory> protocolFactory(new TBinaryProtocolFactory());

    TSimpleServer server(processor, serverTransport, transportFactory, protocolFactory);

    //make tmp dir
    boost::filesystem::path dir(sHandler->get_tmpdir());
    if(!boost::filesystem::create_directory(dir)) {
        //TODO: proper cleanup
        exit(1);
    }


    //configure audio architecture
#ifdef HAS_ALSA
    arch = ALSA;
#elif HAS_OPENAL
    arch = OPENAL_ARCH;
#elif HAS_AUDIOTOOLKIT
    arch = AUDIOTOOLKIT;
#endif
    set_audio(arch),

    sHandler->start();
    server.serve();

    //TODO: proper cleanup

    return 0;
}

