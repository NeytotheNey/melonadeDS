"""A RetroAchievements test server whose one unlock is answered late, for the desktop's "an unlock's reply after its game was unloaded"
check (desktop/src/achievements/Achievements.cpp, Finish).

    python -I tools/ra-late-award-server.py [--port 8766] [--unlock-at FRAMES] [--delay-ms MS]

then run the desktop with --ra-host http://127.0.0.1:8766 --ra-user test --ra-token t0k3n and stop or quit the game while the reply waits
(--frames N a little after the unlock). The game (any hash) has one achievement, 101, which unlocks FRAMES frames (30) after the game's
achievements start; its awardachievement reply is held MS milliseconds (1800, under Achievements::Close's 2 s grace) and says
AchievementsRemaining 0, the answer that sends rc_client into the game's sets. Login (any user, any token), startsession and ping are
answered at once, anything else with {"Success":false}. Each request is printed with the time it came and went. Python's standard library
only; tools/fake-ra-server.py is the full fake server.
"""
import argparse
import json
import sys
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs, urlsplit

GAME_ID = 1
ACHIEVEMENT_ID = 101
START = time.monotonic()


def stamp():
    return f"{time.monotonic() - START:8.3f}"


def answer(args, api, fields, base):
    """(status, JSON object) for one dorequest.php call, after the award's wait."""
    if api == "login2":
        user = fields.get("u", "test")
        return 200, {"Success": True, "User": user, "Token": "t0k3n", "Score": 1000, "SoftcoreScore": 0, "Messages": 0,
                     "AvatarUrl": f"{base}/UserPic/{user}.png"}
    if api == "achievementsets":
        achievement = {"ID": ACHIEVEMENT_ID, "Title": "The only one", "Description": "Unlocks soon after the start", "Flags": 3,
                       "Points": 5, "MemAddr": f"0xH000000=0xH000000.{args.unlock_at}.", "Author": "fake", "BadgeName": "00101",
                       "Created": 1700000000, "Modified": 1700000000, "Type": "", "Rarity": 50.0, "RarityHardcore": 25.0,
                       "BadgeURL": f"{base}/Badge/00101.png", "BadgeLockedURL": f"{base}/Badge/00101_lock.png"}
        return 200, {"Success": True, "GameId": GAME_ID, "Title": "Late Award Game", "ConsoleId": 18,
                     "ImageIconUrl": f"{base}/Images/000001.png", "RichPresenceGameId": GAME_ID, "RichPresencePatch": "",
                     "Sets": [{"AchievementSetId": 1, "GameId": GAME_ID, "Title": "Late Award Game", "Type": "core",
                               "ImageIconUrl": f"{base}/Images/000001.png", "Achievements": [achievement], "Leaderboards": []}]}
    if api == "startsession":
        return 200, {"Success": True, "Unlocks": [], "HardcoreUnlocks": [], "ServerNow": int(time.time())}
    if api == "awardachievement":
        time.sleep(args.delay_ms / 1000.0)
        return 200, {"Success": True, "Score": 1005, "SoftcoreScore": 5, "AchievementID": int(fields.get("a", "0")),
                     "AchievementsRemaining": 0}
    if api == "ping":
        return 200, {"Success": True}
    return 200, {"Success": False, "Error": f"Unknown request: {api}"}


class Handler(BaseHTTPRequestHandler):
    server_version = "LateAwardRA/1"

    def log_message(self, format, *args):
        pass

    def send(self, status, body, content_type):
        try:
            self.send_response(status)
            self.send_header("Content-Type", content_type)
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
        except OSError as error:
            print(f"{stamp()} reply not delivered: {error}", flush=True)

    def dorequest(self, fields):
        api = fields.get("r", "")
        print(f"{stamp()} <- {api}", flush=True)
        status, reply = answer(self.server.args, api, fields, f"http://{self.headers.get('Host', '127.0.0.1')}")
        self.send(status, json.dumps(reply).encode("utf-8"), "application/json")
        print(f"{stamp()} -> {api} {status}", flush=True)

    def do_POST(self):
        length = min(int(self.headers.get("Content-Length", "0") or 0), 1 << 20)
        body = self.rfile.read(length).decode("utf-8", "replace")
        url = urlsplit(self.path)
        if url.path != "/dorequest.php":
            self.send(404, b"not found", "text/plain")
            return
        fields = {k: v[0] for k, v in parse_qs(url.query, keep_blank_values=True).items()}
        fields.update({k: v[0] for k, v in parse_qs(body, keep_blank_values=True).items()})
        self.dorequest(fields)

    def do_GET(self):
        url = urlsplit(self.path)
        if url.path == "/dorequest.php":
            self.dorequest({k: v[0] for k, v in parse_qs(url.query, keep_blank_values=True).items()})
        else:
            self.send(404, b"not found", "text/plain")


def main():
    parser = argparse.ArgumentParser(description="A RetroAchievements test server whose one unlock is answered late")
    parser.add_argument("--port", type=int, default=8766)
    parser.add_argument("--unlock-at", type=int, default=30, help="the achievement's frames after the start")
    parser.add_argument("--delay-ms", type=int, default=1800, help="how long the awardachievement reply is held")
    args = parser.parse_args()
    server = ThreadingHTTPServer(("127.0.0.1", args.port), Handler)
    server.args = args
    print(f"late-award RetroAchievements server on http://127.0.0.1:{args.port}", flush=True)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    return 0


if __name__ == "__main__":
    sys.exit(main())
