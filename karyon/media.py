from __future__ import annotations
import logging
from PyQt6.QtDBus import QDBusConnection, QDBusInterface, QDBusMessage

log = logging.getLogger(__name__)

class MediaBridge:
    def __init__(self):
        self._bus = QDBusConnection.sessionBus()
        self._last_player = None
    
    def _get_player_service(self) -> str | None:
        try:
            msg = QDBusMessage.createMethodCall(
                "org.freedesktop.DBus",
                "/org/freedesktop/DBus",
                "org.freedesktop.DBus",
                "ListNames"
            )
            reply = self._bus.call(msg)
            if reply.type() == QDBusMessage.MessageType.ReplyMessage:
                names = reply.arguments()[0]
                mpris = [n for n in names if n.startswith("org.mpris.MediaPlayer2.")]
                
                best_player = None
                best_status = "Stopped"
                
                for player in mpris:
                    try:
                        iface = QDBusInterface(player, "/org/mpris/MediaPlayer2", "org.freedesktop.DBus.Properties", self._bus)
                        if not iface.isValid(): continue
                        msg = iface.call("Get", "org.mpris.MediaPlayer2.Player", "PlaybackStatus")
                        if msg.type() == QDBusMessage.MessageType.ReplyMessage and msg.arguments():
                            status = msg.arguments()[0]
                            
                            score = {"Playing": 3, "Paused": 2, "Stopped": 1}.get(status, 0)
                            best_score = {"Playing": 3, "Paused": 2, "Stopped": 1}.get(best_status, 0)
                            
                            is_browser = "plasma-browser-integration" in player
                            is_best_browser = best_player and "plasma-browser-integration" in best_player
                            
                            if player == self._last_player:
                                score += 0.5
                            if not is_browser:
                                score += 0.2
                                
                            best_score_adjusted = best_score
                            if best_player == self._last_player:
                                best_score_adjusted += 0.5
                            if not is_best_browser:
                                best_score_adjusted += 0.2
                                
                            if score > best_score_adjusted or best_player is None:
                                best_player = player
                                best_status = status
                    except Exception:
                        pass
                        
                return best_player
        except Exception:
            pass
        return None

    def get_status(self) -> dict | None:
        """Returns {'status': 'Playing'|'Paused'|'Stopped'} or None if no player."""
        player = self._get_player_service()
        if not player:
            return None
        
        try:
            iface = QDBusInterface(
                player,
                "/org/mpris/MediaPlayer2",
                "org.freedesktop.DBus.Properties",
                self._bus
            )
            if not iface.isValid():
                return None
            
            msg = iface.call("Get", "org.mpris.MediaPlayer2.Player", "PlaybackStatus")
            if msg.type() == QDBusMessage.MessageType.ReplyMessage and msg.arguments():
                status = msg.arguments()[0]
                if status == "Stopped":
                    return None
                
                identity = player.split(".")[-1].capitalize()
                try:
                    msg_id = iface.call("Get", "org.mpris.MediaPlayer2", "Identity")
                    if msg_id.type() == QDBusMessage.MessageType.ReplyMessage and msg_id.arguments():
                        identity = msg_id.arguments()[0]
                except Exception:
                    pass
                if identity == "Plasma Browser Integration":
                    identity = "Browser"
                    
                return {"status": status, "player": player, "app_name": identity}
        except Exception:
            log.exception("Failed to retrieve media status")
        return None

    def action(self, cmd: str) -> None:
        """cmd can be 'PlayPause', 'Next', 'Previous'"""
        player = self._get_player_service()
        if not player:
            return
        
        self._last_player = player
        
        try:
            iface = QDBusInterface(
                player,
                "/org/mpris/MediaPlayer2",
                "org.mpris.MediaPlayer2.Player",
                self._bus
            )
            if iface.isValid():
                iface.call(cmd)
                if cmd in ("Next", "Previous"):
                    iface.call("Play")
        except Exception:
            log.exception(f"Media action failed {cmd}")
