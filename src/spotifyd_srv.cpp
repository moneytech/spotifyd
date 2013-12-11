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
#include <boost/program_options.hpp>

#include <iostream>
#include <cstring>
#include <cerrno>
#include <string>

#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "Spotify.h"

#include "spotify_cust.h"
#include "xplodify_sess.h"
#include "xplodify_plc.h"
#include "xplodify_pl.h"


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


XplodifyServer::XplodifyServer(bool multisession)
    : Runnable()
    , Lockable()
    , LOGIN_TO(1)
    , m_ts(std::time(NULL))
    , m_multi(multisession) {
}


void XplodifyServer::run() 
{
    while(!m_done) {
        //is this too much of a busy loop?
        m_io.poll();
        m_io.reset();
    }
}

bool XplodifyServer::is_service_ready() {
    //in monosession, handler is always ready.
    if(!m_multi){
        return true;
    }

    //in multisession handler, check with handler.
    if(!m_sh.handler_available()){
        return true;
    }

    return false;
}

void XplodifyServer::check_in(SpotifyCredential& _return, const SpotifyCredential& cred) {
    std::string uuid_str = m_sh.check_in();
    _return = cred;
    _return.__set__uuid(uuid_str); //Empty string is a failure to check in.
}

bool XplodifyServer::check_out(const SpotifyCredential& cred) {
    return m_sh.check_out(cred._uuid);
}

bool XplodifyServer::loginSession(const SpotifyCredential& cred) {

    bool logging_in = m_sh.login(cred._uuid, cred._username, cred._passwd);

    if(logging_in) {
#ifdef _DEBUG
        std::cout << "starting timer for session: "<< cred._uuid <<"\n";
#endif
        //no transfer of ownership, we're good with raw pointers.
        boost::asio::deadline_timer * t = new boost::asio::deadline_timer(m_io);
        t->expires_from_now(boost::posix_time::seconds(LOGIN_TO));
        t->async_wait(boost::bind(&XplodifyServer::login_timeout,
                    this, boost::asio::placeholders::error, cred._uuid));

        m_timers.insert(std::pair< std::string, boost::asio::deadline_timer *>(cred._uuid, t));

    }
    return logging_in;
}

void XplodifyServer::update_timestamp(void){
    lock();
    m_ts = std::time(NULL);
    unlock();
}

void XplodifyServer::login_timeout(const boost::system::error_code&,
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
    bool logged = m_sh.login_status(uuid);

    if(logged) {
#ifdef _DEBUG
        std::cout << "Session: " << uuid << " Succesfully logged in ...\n";
#endif
        return;
    }

#if 0
    //not logged in, cleanup.
    lock();
    m_sh.check_out(uuid);
    unlock();
#endif

    update_timestamp();
}

bool XplodifyServer::isLoggedIn(const SpotifyCredential& cred) {

    return m_sh.login_status(cred._uuid);
}

int64_t XplodifyServer::getStateTS(const SpotifyCredential& cred) {

    return m_ts;
}

int64_t XplodifyServer::getSessionStateTS(const SpotifyCredential& cred) {

    int64_t ts;

    ts = m_sh.get_session_state(cred._uuid);
    return ts;
}

void XplodifyServer::logoutSession(const SpotifyCredential& cred) {

    m_sh.logout(cred._uuid);

    update_timestamp();
    return;
}

void XplodifyServer::sendCommand(const SpotifyCredential& cred, const SpotifyCmd::type cmd) {
    std::cout << "sendCommand" << std::endl;

    //rethink role for cred._uuid for this RPC call.
    switch(cmd){
        case SpotifyCmd::PLAY:
            m_sh.start();
            break;
        case SpotifyCmd::PAUSE:
            m_sh.stop();
            break;
        case SpotifyCmd::NEXT:
        case SpotifyCmd::PREV:
#if 0
            sess->end_of_track();
            break;
#endif
        case SpotifyCmd::RAND:
        case SpotifyCmd::LINEAR:
        case SpotifyCmd::REPEAT_ONE:
        case SpotifyCmd::REPEAT:
#if 0
            sess->set_mode(cmd);
            break;
#endif
        default:
            break;
    }

    update_timestamp();
    return;
}


void XplodifyServer::search(SpotifyPlaylist& _return, const SpotifyCredential& cred,
		const SpotifySearch& criteria) {
    // Your implementation goes here
    printf("search\n");
#if 0
    boost::shared_ptr<XplodifySession> sess = get_session(cred._uuid);
    if(!sess) {
        return;
    }
#endif
}

void XplodifyServer::getPlaylists(SpotifyPlaylistList& _return, const SpotifyCredential& cred) 
{

    std::vector< boost::shared_ptr<XplodifyPlaylist> > playlists(m_sh.get_playlists(cred._uuid));
    for(int i=0 ; i<playlists.size() ; i++) {
        std::string plstr(playlists[i]->get_name(true));
        _return.insert(plstr);
    }

    return;

}

void XplodifyServer::getPlaylist(SpotifyPlaylist& _return, const SpotifyCredential& cred,
        const int32_t plist_id) {

    std::vector< boost::shared_ptr<XplodifyTrack> > tracks(m_sh.get_tracks(cred._uuid, plist_id));

    for(unsigned int i = 0 ; i < tracks.size() ; i++ ) {
        boost::shared_ptr<XplodifyTrack> tr = tracks[i];

        int duration = tr->get_duration(true); //millisecs?
        SpotifyTrack spt;

        spt.__set__id( tr->get_index(true) );
        spt.__set__name( tr->get_name(true) );
        spt.__set__artist( tr->get_artist(0, true) ); //first artist (this sucks).
        spt.__set__minutes( duration / 60000 );
        spt.__set__seconds( (duration / 1000) % 60 );
        spt.__set__popularity( tr->get_popularity(true) );
        spt.__set__starred( tr->is_starred(true) );
        spt.__set__genre( "unknown" );
        _return.push_back(spt);
    }

    return;
}

void XplodifyServer::getPlaylistByName(
        SpotifyPlaylist& _return, const SpotifyCredential& cred,
        const std::string& name) {

    std::vector< boost::shared_ptr<XplodifyTrack> > tracks(m_sh.get_tracks(cred._uuid, name));

    for(unsigned int i = 0 ; i < tracks.size() ; i++ ) {
        boost::shared_ptr<XplodifyTrack> tr = tracks[i];

        int duration = tr->get_duration(true); //millisecs?
        SpotifyTrack spt;

        spt.__set__id( tr->get_index(true) );
        spt.__set__name( tr->get_name(true) );
        spt.__set__artist( tr->get_artist(0, true) ); //first artist (this sucks).
        spt.__set__minutes( duration / 60000 );
        spt.__set__seconds( (duration / 1000) % 60 );
        spt.__set__popularity( tr->get_popularity(true) );
        spt.__set__starred( tr->is_starred(true) );
        spt.__set__genre( "unknown" );
        _return.push_back(spt);
    }

    return;
}

void XplodifyServer::selectPlaylist(const SpotifyCredential& cred, const std::string& playlist) {
    m_sh.select_playlist(cred._uuid, playlist);
    return;
}

void XplodifyServer::selectPlaylistById(const SpotifyCredential& cred, const int32_t plist_id) {
    m_sh.select_playlist(cred._uuid, plist_id);
    return;
}

void XplodifyServer::selectTrack(const SpotifyCredential& cred, const std::string& track) {
    m_sh.select_track(cred._uuid, track);
    return;
}

void XplodifyServer::selectTrackById(const SpotifyCredential& cred, const int32_t track_id) {
    m_sh.select_track(cred._uuid, track_id);
    return;
}

bool XplodifyServer::merge2playlist(const SpotifyCredential& cred, const std::string& pl,
        const SpotifyPlaylist& tracks) {
    // Your implementation goes here
    printf("merge2playlist\n");
    return true;
}

bool XplodifyServer::add2playlist(const SpotifyCredential& cred, const std::string& pl,
        const SpotifyTrack& track) {
    // Your implementation goes here
    printf("add2playlist\n");
    return true;
}

void XplodifyServer::whats_playing(SpotifyTrack& _return, const SpotifyCredential& cred ) {
    boost::shared_ptr<XplodifyTrack> tr = m_sh.whats_playing(cred._uuid);
    if(!tr) {
        return;
    }

    int duration = tr->get_duration(true); //millisecs?
    _return.__set__id( tr->get_index(true) );
    _return.__set__name( tr->get_name(true) );
    _return.__set__artist( tr->get_artist(0, true) ); //first artist (this sucks).
    _return.__set__minutes( duration / 60000 );
    _return.__set__seconds( (duration / 1000) % 60 );
    _return.__set__popularity( tr->get_popularity(true) );
    _return.__set__starred( tr->is_starred(true) );
    _return.__set__genre( "unknown" );

    return;
}


namespace {
    const uint32_t SRV_BASE_PORT=9090;
}

int main(int argc, char **argv) {
    uint32_t port, child_port;
    bool multi = true;

    pid_t master_pid = 0, slave_pid = 0;

    namespace po = boost::program_options; 
    po::options_description desc("Options"); 
    desc.add_options() 
        ("help", "Print help messages") 
        ("port", po::value<uint32_t>(&port)->default_value(SRV_BASE_PORT),"base port for server") 
        ("mono", "server mode: mono session enabled."); 

    po::variables_map vm;
    po::store(po::parse_command_line(argc, argv, desc), vm);
    po::notify(vm);

    if(vm.count("help")) {
        std::cout << desc << std::endl;
        return 1;
    }

    if(vm.count("mono")) {
        multi = false;
        child_port = port + 1;
    }

    if(multi) {
        master_pid = fork();
        if(!master_pid) { //MASTER CHILD process
            //TODO Boost service.
            exit(0);
        }

        slave_pid = fork();
        if(!slave_pid) { //SLAVE CHILD process
            //TODO> Boost service.
            exit(0);
        }
    }

    //XplodifyServer
    boost::shared_ptr<XplodifyServer> spserver(new XplodifyServer(multi));

    //THRIFT Server
    boost::shared_ptr<TProcessor> processor(new SpotifyProcessor(spserver));
    boost::shared_ptr<TServerTransport> serverTransport(new TServerSocket(port));
    boost::shared_ptr<TTransportFactory> transportFactory(new TBufferedTransportFactory());
    boost::shared_ptr<TProtocolFactory> protocolFactory(new TBinaryProtocolFactory());

    TSimpleServer server(processor, serverTransport, transportFactory, protocolFactory);


    spserver->start();
    server.serve();

    //TODO: proper cleanup
    int status;
    pid_t done;

    if(multi) {
        if(!(done = waitpid(master_pid, &status, 0))) {
            std::cerr << strerror(errno) << std::endl;
            return -1;
        }
        if(!(done = waitpid(slave_pid, &status, 0))) {
            std::cerr << strerror(errno) << std::endl;
            return -1;
        }
    }

    return 0;
}

