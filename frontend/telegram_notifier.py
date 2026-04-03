"""
Telegram Notifier
-----------------
- Polls for /start commands and registers new subscribers
- Persists chat IDs to subscribers.json
- Broadcasts crowd alerts to all subscribers on level change
"""

import json
import logging
import threading
from pathlib import Path

import requests

log = logging.getLogger("telegram_notifier")

SUBSCRIBERS_FILE = Path(__file__).parent / "subscribers.json"


class TelegramNotifier:
    def __init__(self, token: str):
        self.token    = token
        self.base_url = f"https://api.telegram.org/bot{token}"
        self.subscribers: set[str] = self._load_subscribers()
        self.last_snapshot: dict = {}
        self._offset    = 0
        self._running   = False
        self._thread    = None

    # ── Persistence ──────────────────────────────────────────────────────────

    def _load_subscribers(self) -> set:
        if SUBSCRIBERS_FILE.exists():
            try:
                return set(json.loads(SUBSCRIBERS_FILE.read_text()))
            except Exception:
                pass
        return set()

    def _save_subscribers(self):
        SUBSCRIBERS_FILE.write_text(json.dumps(list(self.subscribers), indent=2))

    # ── Low-level send ────────────────────────────────────────────────────────

    def _send_to(self, chat_id: str, text: str):
        try:
            resp = requests.post(
                f"{self.base_url}/sendMessage",
                json={"chat_id": chat_id, "text": text, "parse_mode": "Markdown"},
                timeout=5,
            )
            if resp.status_code == 403:
                log.warning("Chat %s blocked the bot — removing from subscribers", chat_id)
                self.subscribers.discard(chat_id)
                self._save_subscribers()
                return
            resp.raise_for_status()
        except Exception as exc:
            log.warning("Telegram send to %s failed: %s", chat_id, exc)

    # ── Long-poll loop (runs in background thread) ────────────────────────────

    def _poll(self):
        log.info("Telegram poll thread started")
        while self._running:
            try:
                resp = requests.get(
                    f"{self.base_url}/getUpdates",
                    params={"offset": self._offset, "timeout": 10},
                    timeout=15,
                )
                for update in resp.json().get("result", []):
                    self._offset = update["update_id"] + 1
                    self._handle_update(update)
            except Exception as exc:
                log.warning("Telegram poll error: %s", exc)

    def _handle_update(self, update: dict):
        message    = update.get("message", {})
        text       = message.get("text", "")
        chat       = message.get("chat", {})
        chat_id    = str(chat.get("id", ""))
        first_name = message.get("from", {}).get("first_name", "there")

        if not chat_id or not text.startswith("/start"):
            return

        already_subscribed = chat_id in self.subscribers
        self.subscribers.add(chat_id)
        self._save_subscribers()

        if already_subscribed:
            self._send_to(
                chat_id,
                f"Hi {first_name}! You are already subscribed to *Canteen Crowd Alerts*. "
                "You will continue to receive notifications when the crowd level changes.",
            )
        else:
            log.info("New subscriber: chat_id=%s  name=%s  (total=%d)", chat_id, first_name, len(self.subscribers))
            self._send_to(
                chat_id,
                f"Hi {first_name}! You have successfully subscribed to *Canteen Crowd Alerts*.\n\n"
                "You will receive a notification whenever the crowd level changes "
                "(Low / Medium / High).\n\n"
                "_Stay tuned — the next update will arrive automatically._",
            )

    # ── Public API ────────────────────────────────────────────────────────────

    def start_polling(self):
        """Start background thread that listens for /start commands."""
        self._running = True
        self._thread  = threading.Thread(
            target=self._poll, daemon=True, name="telegram-poll"
        )
        self._thread.start()
        log.info(
            "Telegram bot ready — %d existing subscriber(s)", len(self.subscribers)
        )

    def stop(self):
        self._running = False

    def send(self, data: dict):
        """Broadcast a crowd update to all subscribers. Fires whenever any value changes."""
        snapshot = {
            "totalPeople": data.get("totalPeople"),
            "level":       data.get("level"),
            "trend":       data.get("trend"),
            "bestTime":    data.get("bestTime"),
            "avgTemp":     data.get("avgTemp"),
            "zones":       data.get("zones"),
        }
        if snapshot == self.last_snapshot:
            return
        self.last_snapshot = snapshot

        if not self.subscribers:
            log.debug("No subscribers — skipping Telegram alert")
            return

        level = snapshot["level"] or "UNKNOWN"
        trend = snapshot["trend"] or "UNKNOWN"

        level_emoji = {"LOW": "🟢", "MEDIUM": "🟡", "HIGH": "🔴"}.get(level, "⚪")
        trend_emoji = {"INCREASING": "📈", "DECREASING": "📉", "STABLE": "➡️"}.get(trend, "")
        best_time   = data.get("bestTime", "?")
        best_emoji  = {"NOW": "✅", "SOON": "⏳", "LATER": "🕐", "AVOID(PEAK)": "🚫"}.get(best_time, "")

        zones_raw = data.get("zones", {})
        if isinstance(zones_raw, dict):
            zone_lines = []
            for k, v in zones_raw.items():
                label = str(v).strip().title()
                z_emoji = {"Low": "🟢", "Medium": "🟡", "High": "🔴"}.get(label, "⚪")
                zone_lines.append(f"  {z_emoji} Zone {k}: {label}")
            zones_text = "\n".join(zone_lines)
        else:
            zones_text = "  —"

        temp = data.get("avgTemp") or data.get("temperature")
        temp_str = f"{float(temp):.1f}°C" if temp is not None else "—"

        msg = (
            f"{level_emoji} *Canteen Crowd Alert*\n"
            f"━━━━━━━━━━━━━━━━━━\n"
            f"👥 *People:* {data.get('totalPeople', '?')}\n"
            f"{level_emoji} *Level:* {level}\n"
            f"{trend_emoji} *Trend:* {trend}\n"
            f"{best_emoji} *Best time to visit:* {best_time}\n"
            f"🌡️ *Temperature:* {temp_str}\n"
            f"\n"
            f"📍 *Seating Zones*\n"
            f"{zones_text}\n"
            f"\n"
            f"📊 *Details*\n"
            f"  Density: `{float(data.get('density', 0)):.2f}`  |  Score: `{float(data.get('score', 0)):.2f}`\n"
            f"\n"
            f"━━━━━━━━━━━━━━━━━━\n"
            f"_⚠️ Values shown are estimated readings._"
        )

        for chat_id in list(self.subscribers):
            self._send_to(chat_id, msg)

        log.info(
            "Telegram alert sent to %d subscriber(s) → level=%s",
            len(self.subscribers), level,
        )
