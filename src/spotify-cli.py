#!/usr/bin/env python

# -*- coding: latin-1 -*-

import sys
import time
import getopt
import os.path

#temporary hack, we'll be moving to virtualenv.
sys.path.insert(1, './gen-py')

from spotify import Spotify
from spotify.ttypes import *
from spotify.constants import *

import thrift
from thrift.transport import TSocket
from thrift.transport import TTransport
from thrift.protocol import TBinaryProtocol

import urwid

import threading
import signal
import getpass

import logging

XPLODIFYD_PORT = 9090
VERSION = "0.1"

POLL_IVAL = 2 #in seconds

logging.basicConfig(filename='debug.log',level=logging.DEBUG)

def signal_handler(signal, frame):
    sys.exit(0)


class spclient(object):

    def __init__(self, host, port):
        # Make socket
        self._transport = TSocket.TSocket(host, port)

        # Buffering is critical. Raw sockets are very slow
        self._transport = TTransport.TBufferedTransport(self._transport)

        # Wrap in a protocol
        self._protocol = TBinaryProtocol.TBinaryProtocol(self._transport)

        # Create a client to use the protocol encoder
        self._client = Spotify.Client(self._protocol)

        # Connect!
        try:
            self._transport.open()
        except thrift.transport.TTransport.TTransportException:
            print "Couldn't connect to server on {}:{}".format(host, port)
            sys.exit(1)

        #status
        self._success = True

        # Empty credential
        self._credentials = None

        # Empty playlist array
        self._playlists = []

        # Empty selected playlist
        self._currentplaylist = None

        # Checked In
        self.checked_in = False

    def service_ready(self):
        return self._client.is_service_ready()

    def check_in(self):
        try:
            credentials = SpotifyCredential()
            self._credentials = self._client.check_in(credentials)
            if self._credentials._uuid:
                self._checked_in = True
        except Exception, e:
            logging.debug("Exception: %s", e)
            return False

        return self._checked_in

    def login(self, username, password):
        success = False
        if not self._checked_in:
            success = self.check_in()
            if not success:
                return False

        success = False
        try:
            self._credentials._username = username
            self._credentials._passwd = password
            self._credentials = self._client.loginSession(self._credentials)
            success = True   # refine this.
        except Exception, e:
            logging.debug("Exception: %s", e)
            return False

        return success

    def logout(self):
        try:
            if self._credentials:
                self._client.logoutSession(self._credentials)
                # we'll see how we handle logout and checkouts... leave as is for now.
                self._credentials = None
        except Exception, e:
            logging.debug("Exception: %s", e)
            return False

        return True

    """"
    Because libspotify is async we need this to check we logged
    in succesfully.
    """
    def loggedin(self, username=None, uid=None):
        ret = False
        if username is None and uid is None:
            return ret

        try:
            credentials = SpotifyCredential(username)
            ret = self._client.isLoggedIn(credentials)

        except Exception, e:
            logging.debug("Exception: %s", e)
            self._success = False
        finally:
            return ret

    """
    Handler state. This can be potentially different from session
    state if and when the multi-session server is implemented.
    Reflects server/handler state.
    """
    def get_state(self):
        state = None
        try:
            state = self._client.getStateTS(self._credentials)
        except Exception, e:
            logging.debug("Exception: %s", e)
            self._success = False

        return state

    """
    Handler state. This can be potentially different from handler
    state if and when the multi-session server is implemented.
    Reflects user session state..
    """
    def get_session_state(self):
        state = None
        try:
            state = self._client.getSessionStateTS(self._credentials)
        except Exception, e:
            logging.debug("Exception: %s", e)
            self._success = False

        return state

    def whats_playing(self):
        song = None
        try:
            song = self._client.whats_playing()
        except Exception, e:
            logging.debug("Exception: %s", e)
            self._success = False

        return song

    def get_playlists(self):
        pls = None
        try:
            """ pls will be a set with the playlists """
            pls = self._client.getPlaylists(self._credentials)

        except Exception, e:
            logging.debug("Exception: %s", e)
            self._success = False

        return pls

    def get_tracks(self, playlist):
        tracks = None
        try:
            """ pls will be a set with the playlists """
            tracks = self._client.getPlaylistByName(
                self._credentials,
                playlist
            )

        except Exception, e:
            logging.debug("Exception: %s", e)
            self._success = False

        return tracks

    def select_playlist(self, playlist):
        try:
            self._client.selectPlaylist(self._credentials, playlist)
            self._success = True

        except Exception, e:
            logging.debug("Exception: %s", e)
            self._success = False

        return None

    def select_playlist_by_id(self, pl_id):
        try:
            self._client.selectPlaylist(self._credentials, pl_id)
            self._success = True

        except Exception, e:
            logging.debug("Exception: %s", e)
            self._success = False

        return None

    def select_track(self, track_name):
        try:
            self._client.selectTrack(self._credentials, track_name)
            self._success = True

        except Exception, e:
            logging.debug("Exception: %s", e)
            self._success = False

        return None

    def select_track_id(self, track_id):
        try:
            self._client.selectTrackById(self._credentials, track_id)
            self._success = True

        except Exception, e:
            logging.debug("Exception: %s", e)
            self._success = False

        return None

    def spot_playback(self, cmd):
        try:
            #keeping it real simple for now.
            self._client.sendCommand(self._credentials, cmd)

        except Exception, e:
            logging.debug("Exception: %s", e)
            self._success = False
            return None

    def spot_getcurrent(self):
        return None


class XplodifyElement(urwid.Button):
    def __init__(self, el_id, el_name, callback=None, userdata=None):
        self.el_id = el_id
        self.el_name = el_name
        super(XplodifyElement, self).__init__(
            self.el_name,
            on_press=callback,
            user_data=userdata
        )


class XplodifyApp(urwid.Frame):
    palette = [
        ('body','default', 'default'),
        ('reversed','light gray', 'black', 'bold'),
        ('playback','light red', 'dark gray', 'bold'),
        ('foot','black', 'light gray', 'bold'),
        ('log','dark blue', 'black', 'bold'),
        ('track','light red', 'black', 'bold'),
        ('key','light cyan', 'dark blue', 'underline'),
    ]

    footer_text = ('foot', [
        "Xpldofiy Client " + VERSION + " -     ",
        ('key', "F4"), " login   ",
        ('key', "F6"), " |<   ",
        ('key', "F7"), " |> / ||   ",
        ('key', "F8"), " >|   ",
        ('key', "F10"), " logout  ",
        ('key', "F11"), " quit ",
        ])

    def __init__(self, host='localhost', port=XPLODIFYD_PORT):
        self.logged = False
        self.spoticlient = spclient(host, port)

        #get UUID
        self.spoticlient.check_in()

        self._playlists = None
        self._active_pl = None
        self._active_pl_button = None
        self._plwalker = urwid.SimpleFocusListWalker([])
        self._tracks = {}
        self._active_tr_button = None
        self._current_state = SpotifyCmd.PAUSE
        self._trwalker = urwid.SimpleFocusListWalker([])
        self.plpane = urwid.ListBox(self._plwalker)
        self.trackpane = urwid.ListBox(self._trwalker)
        self.logwid = urwid.AttrWrap(urwid.Text(u"Not Logged In."), "log")
        self.trackwid = urwid.AttrWrap(urwid.Text(u"Now playing: (none)"), "track")
        self.header = urwid.Pile([self.logwid, self.trackwid])
        self.footer = urwid.AttrWrap(urwid.Text(self.footer_text), "foot")

        self._poller = None
        self._state = 0
        self._sess_state = 0
        self._mutex = threading.Lock()
        self._stop = threading.Event()

        self.widgets = [
            self.plpane,
            self.trackpane
        ]

        email = urwid.Edit(
            u'Username:  ',
            u"",
            allow_tab=False,
            multiline=False
        )

        passwd = urwid.Edit(
            u'Password:  ',
            u"",
            allow_tab=False,
            multiline=False,
            mask=u"*"
        )
        logbutton = urwid.Button(u'Login')
        urwid.connect_signal(logbutton, 'click', self.login)

        self.mainview = urwid.Columns(
            self.widgets,
            dividechars=1,
            focus_column=0
        )
        self.loginview = urwid.Filler(urwid.Pile([
            email,
            passwd,
            urwid.AttrMap(logbutton, None, focus_map='reversed')
        ]))
        self.overlay = urwid.Overlay(
            self.loginview,
            self.mainview,
            align='center', width=('relative', 30),
            valign='middle', height=('relative', 30),
            min_width=30, min_height=6
        )
        super(XplodifyApp, self).__init__(
            urwid.AttrWrap(self.mainview, 'body'),
            footer=self.footer,
            header=self.header
        )

        urwid.set_encoding("ISO-8859-*")
        self.loop = urwid.MainLoop(
            self,
            self.palette,
            unhandled_input=self.unhandled_keypress
        )

    def main(self):
        self.loop.run()

    def login_overlay(self):
        if self.logged:
            pass
        else:
            self.body = self.overlay

    def poll(self):
        while not self._stop.is_set():
            redraw = False
            threading.Event().wait(POLL_IVAL)
            self._mutex.acquire()
            try:
                cur_st = self.spoticlient.get_state()
                if cur_st > self._state:
                    self._state = cur_st
                    cur_song = self.spoticlient.whats_playing()
                    if cur_song and cur_song._name:
                        self.trackwid.original_widget.\
                            set_text(u"Now playing: "+cur_song._name)
                        self.highlight_track(cur_song._name)
                        redraw = True

                # no need to do this as often...
                cur_sess_st = self.spoticlient.get_session_state()
                if cur_sess_st > self._sess_state:
                    self._sess_state = cur_sess_st
                    # typically user's playlists have changed
                    self.get_playlists(active_pl = self._active_pl)
                    redraw = True
            except Exception, e:
                logging.debug("Error in polling thread. Exception %s", e)
            finally:
                self._mutex.release()
                if redraw:
                    self.loop.draw_screen()

    def login(self, key):
        if not self.spoticlient.service_ready():
            # TODO: bring up a warning for a couple seconds... will do this different with webUI
            self.body = self.mainview
            return

        username = self.loginview.original_widget.\
            widget_list[0].get_edit_text()
        passwd = self.loginview.original_widget.\
            widget_list[1].get_edit_text()
        if not self.logged:
            self.logged = self.spoticlient.login(username, passwd)
        time.sleep(8)
        if self.logged:
            self.logwid.original_widget.set_text(u"Logged in as "+username)
            logging.debug("Retrieving playlists.")

            self.get_playlists()
            self.mainview.set_focus_column(0)
            self._poller = threading.Thread(target=self.poll)
            self._poller.start()

        self.body = self.mainview

    def logout(self):
        if self.logged:
            self.spoticlient.logout()

            self._active_pl = None
            self._active_pl_button = None

            self.clear_pl_panel()
            self.clear_track_panel()
            self.logwid.original_widget.set_text(u"Not Logged in.")
            self.trackwid.original_widget.set_text(u"Now playing: (none)")
            self.loginview.original_widget.widget_list[0].set_edit_text(u"")
            self.loginview.original_widget.widget_list[1].set_edit_text(u"")
            self.loginview.original_widget.focus_position = 0
            self._stop.set()
            self._poller.join(POLL_IVAL+1)
            if self._poller.isAlive(): 
                exit(1) # this might be too much.

            self.logged = False

    def adjust_pl_pid(self, pl, pid):
        for pl in self._plwalker:
            if pl.original_widget.el_name == pl:
                pl.original_widget.el_id = pid
                break

    def get_playlists(self, active_pl=None):
        try:
            self._playlists = list(self.spoticlient.get_playlists())
            logging.debug("Retrieved %d playlists: ", len(self._playlists))
        except Exception, e:
            self._playlists = []
            logging.debug("Exception: %s", e)

        if self._playlists:
            """
            while self._plwalker:
                self._plwalker.pop()
            """

            existing = [] #names
            removed = [] #indices
            for pl in self._plwalker:
                try:
                    pos = self._playlists.index(pl.original_widget.el_name)
                    existing.append(pl.original_widget.el_name)
                except Exception, e:
                    removed.append(self._plwalker.index(pl), pl.original_widget.el_name)

            #remove deleted playlists...
            for i,pl in sorted(removed, reverse=True):
                del self._tracks[pl]
                del self._plwalker[i]

            #refresh
            pid = 1
            for pl in self._playlists:
                try:
                    if pl not in existing:
                        self._plwalker.insert(0, urwid.AttrMap(
                            XplodifyElement(
                                pid,
                                pl,
                                callback=self.set_track_panel,
                                userdata=pl
                            ),
                            None, focus_map='reversed'))
                    else:
                        self.adjust_pl_pid(pl, pid)

                    self.get_tracks(pl)
                    pid += 1
                except Exception, e:
                    logging.debug("Exception: %s", e)

            if active_pl:
                for btn in self._plwalker:
                    if btn.original_widget.el_name == active_pl:
                        pos = self._plwalker.index(btn)
                        self._plwalker.set_focus(pos)
                        self.set_track_panel(btn, active_pl)
                        break
            else:
                self.set_track_panel(None, self._playlists[0])

    def get_tracks(self, playlist):
        try:
            self._tracks[playlist] = self.spoticlient.get_tracks(playlist)
        except Exception, e:
            logging.debug("Exception: %s", e)

    def set_track_panel(self, button, playlist):
        if playlist not in self._tracks:
            self.get_tracks(playlist)

        if self._tracks[playlist]:
            self.clear_track_panel()
            self.spoticlient.select_playlist(playlist)
            self._active_pl = playlist

            #remove highlight to old button
            if self._active_pl_button:
                self._active_pl_button.set_attr_map({'reversed': None})

            #if button, set highlight - playlist was selected.
            if button:
                w, pos = self._plwalker.get_focus()
                w.set_attr_map({None: 'reversed'})
                self._active_pl_button = w

            for track in self._tracks[playlist]:
                logging.debug("Processing track: %s", track._name)
                self._trwalker.insert(
                    0,
                    urwid.AttrMap(
                        XplodifyElement(
                            track._id,
                            track._name+" - "+track._artist,
                            callback=self.playback_track,
                            userdata=track
                        ),
                        None,
                        focus_map='reversed'
                    )
                )

    def clear_pl_panel(self):
        while self._plwalker:
            self._plwalker.pop()
        self._active_pl_button = None
        self._active_tr = None

    def clear_track_panel(self):
        while self._trwalker:
            self._trwalker.pop()
        self._active_tr_button = None

    def highlight_track(self, track):
        for tr in self._trwalker:
            if tr.original_widget.el_name == track:
                # remove old highlight
                if self._active_tr_button:
                    self._active_tr_button.set_attr_map({'playback': None})
                self._active_tr_button = tr
                tr.set_attr_map({None: 'playback'})
                break

    def playback_track(self, button, track):
        #remove highlight to old track button
        if self._active_tr_button:
            self._active_tr_button.set_attr_map({'playback': None})

        #if button, set highlight - playlist was selected.
        if button:
            w, pos = self._trwalker.get_focus()
            w.set_attr_map({None: 'playback'})
            self._active_tr_button = w

        if self._current_state == SpotifyCmd.PLAY:
            self.toggle_playback()

        logging.debug("Selecting track: %s", track._name)

        self.spoticlient.select_track(track._name)
        time.sleep(2)
        self.toggle_playback()

    def toggle_playback(self):
        logging.debug("Toggling playback")
        try:
            if self._current_state == SpotifyCmd.PAUSE:
                self.spoticlient.spot_playback(SpotifyCmd.PLAY)
                self._current_state = SpotifyCmd.PLAY
            elif self._current_state == SpotifyCmd.PLAY:
                self.spoticlient.spot_playback(SpotifyCmd.PAUSE)
                self._current_state = SpotifyCmd.PAUSE
        except Exception, e:
            logging.debug("Exception: %s", e)

    def playback_cmd(self, cmd):
        logging.debug("Modifying playback")
        try:
            if self._current_state == SpotifyCmd.PAUSE:
                self.spoticlient.spot_playback(cmd)
                self._current_state = SpotifyCmd.PLAY
            else:
                self.spoticlient.spot_playback(cmd)
        except Exception, e:
            logging.debug("Exception: %s", e)

    def set_playlist(self, button, playlist):
        return

    def quit(self):
        raise urwid.ExitMainLoop()

    def unhandled_keypress(self, k):

        if k == "f4":
            self.login_overlay()
        elif k == "f5":
            raise urwid.ExitMainLoop()
        elif k == "f6":
            self.playback_cmd(SpotifyCmd.PREV)
        elif k == "f7":
            self.toggle_playback()
        elif k == "f8":
            self.playback_cmd(SpotifyCmd.NEXT)
        elif k == "f10":
            self.logout()
        elif k == "f11":
            self.logout()
            raise urwid.ExitMainLoop()
        elif k == "tab":
            self.mainview.focus_position = (
                self.mainview.focus_position + 1
            ) % 2
        elif k == "esc":
            self.body = self.mainview
        else:
            return

        return True


def usage():
    BOLD_START = "\033[1m"
    BOLD_END = "\033[0m"
    print "usage: "+os.path.basename(__file__)+" [OPTIONS]"
    print "\nOPTIONS:"
    print "\t"+BOLD_START+"-h"+BOLD_END+"\n\t\tDisplay this help message."
    print "\t"+BOLD_START+"-s host,--server=host"+BOLD_END+"\n\t\tSpecify service hostname."
    print "\t"+BOLD_START+"-p PORT,--port=PORT"+BOLD_END+"\n\t\tSpecify service port."


def main(argv):
    server = 'localhost'
    port = XPLODIFYD_PORT

    try:
        opts, args = getopt.getopt(argv, "hs:p:", ["server=", "port="])
    except getopt.GetoptError:
        usage()
        sys.exit(2)

    for opt, arg in opts:
        if opt == '-h':
            usage()
            sys.exit()
        elif opt in ("-s", "--server"):
            server = arg
        elif opt in ("-p", "--port"):
            port = int(arg)

    XplodifyApp(host=server, port=port).main()


if __name__ == '__main__':
    main(sys.argv[1:])
