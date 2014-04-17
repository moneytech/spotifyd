#!/opt/local/bin/thrift --gen cpp

include "spotify_types.thrift"

service SpotifyIPC {
	bool set_master();
	bool set_slave();
	spotify_types.SpotifyCredential check_in(1: spotify_types.SpotifyCredential cred);
	bool check_out();
	bool login(1: spotify_types.SpotifyCredential cred);
	bool is_logged();
	oneway void logout();
	bool playback_done();

	oneway void selectPlaylist(1: string playlist);
	oneway void selectPlaylistById(1: i32 plist_id);
	oneway void selectTrack(1: string track);
	oneway void selectTrackById(1: i32 track_id);

	oneway void play();
	oneway void stop();
	oneway void terminate_proc();

	spotify_types.SpotifyTrack whats_playing();
	spotify_types.SpotifyTrack whats_next();
	spotify_types.SpotifyTrack track_loaded(1: spotify_types.SpotifyCredential cred);
	spotify_types.SpotifyPlaylist playlist_loaded(1: spotify_types.SpotifyCredential cred);
}
