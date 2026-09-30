#!/usr/bin/env python3
"""CM4 viewer for source-rendered app screenshots; never connects to ESP32."""
from pathlib import Path
import argparse
import os
import signal
import subprocess
import sys
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "build/preview-ui"


class Renderer:
    def __init__(self):
        self.log = tempfile.TemporaryFile(mode="w+b")
        self.child = subprocess.Popen([sys.executable, str(ROOT / "tools/preview_ui.py")],
                                      cwd=ROOT, stdout=self.log, stderr=subprocess.STDOUT,
                                      start_new_session=True)
        self.started = time.monotonic()

    def stop(self):
        if self.child.poll() is None:
            os.killpg(self.child.pid, signal.SIGKILL)
            self.child.wait()

    def close(self):
        self.stop()
        self.log.close()

    def error(self):
        self.log.seek(0, os.SEEK_END)
        self.log.seek(max(0, self.log.tell() - 6000))
        return self.log.read().decode(errors="replace")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--render-only", action="store_true")
    parser.add_argument("--smoke-seconds", type=int, default=0)
    args = parser.parse_args()
    if args.render_only:
        renderer = Renderer()
        try:
            try:
                code = renderer.child.wait(timeout=180)
            except subprocess.TimeoutExpired:
                print("Preview rendering timed out", file=sys.stderr)
                return 1
            if code:
                print(renderer.error(), file=sys.stderr)
            else:
                print(OUTPUT)
            return code
        finally:
            renderer.close()

    import gi
    gi.require_version("Gtk", "3.0")
    from gi.repository import Gdk, GdkPixbuf, GLib, Gtk

    window = Gtk.Window(title="DIY 应用源码预览")
    window.set_default_size(800, 600)
    box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=6)
    window.add(box)
    bar = Gtk.Box(spacing=8)
    box.pack_start(bar, False, False, 0)
    selector = Gtk.ComboBoxText()
    pages = [("时钟", "clock-dark.png"), ("MIDI 小乐器", "instrument-dark.png"),
             ("按住琴键", "instrument-held-dark.png"), ("应用列表", "apps-dark.png"),
             ("计算器", "calculator-dark.png"), ("计算器：亮色", "calculator-light.png"),
             ("计算器：错误", "calculator-error-dark.png"), ("日历", "calendar-dark.png"),
             ("日历：亮色", "calendar-light.png"), ("日历：未对时", "calendar-unsynced-dark.png"),
             ("2048", "2048-dark.png"), ("2048：亮色", "2048-light.png"),
             ("2048：获胜", "2048-win-dark.png"), ("2048：结束", "2048-over-dark.png")]
    for title, _name in pages:
        selector.append_text(title)
    selector.set_active(0)
    bar.pack_start(selector, False, False, 0)
    refresh = Gtk.Button(label="重新渲染 F5")
    bar.pack_start(refresh, False, False, 0)
    leave = Gtk.Button(label="退出 Esc")
    bar.pack_end(leave, False, False, 0)
    status = Gtk.Label(label="源码绘图 · 演示数据 · 图片预览不接收琴键/触摸操作")
    box.pack_start(status, False, False, 0)
    area = Gtk.ScrolledWindow()
    box.pack_start(area, True, True, 0)
    picture = Gtk.Image()
    area.add(picture)
    renderer = None
    poll_id = None
    valid = False

    def show_page(*_):
        if not valid:
            return
        path = OUTPUT / pages[selector.get_active()][1]
        # Preserve 4:3 geometry on CM4; original 1024x768 images remain on disk.
        picture.set_from_pixbuf(GdkPixbuf.Pixbuf.new_from_file_at_scale(str(path), 720, 540, True))

    def finish():
        nonlocal renderer, poll_id, valid
        if renderer.child.poll() is None and time.monotonic() - renderer.started <= 180:
            return GLib.SOURCE_CONTINUE
        timed_out = renderer.child.poll() is None
        if timed_out:
            renderer.stop()
        valid = not timed_out and renderer.child.returncode == 0
        refresh.set_sensitive(True)
        if valid:
            status.set_text("源码绘图 · 演示数据 · 图片预览不接收琴键/触摸操作")
            show_page()
        else:
            picture.clear()
            status.set_text("渲染超时" if timed_out else "渲染失败，详情见启动终端；旧图已隐藏")
            print(renderer.error(), file=sys.stderr)
        renderer.close()
        renderer = None
        poll_id = None
        return GLib.SOURCE_REMOVE

    def start(*_):
        nonlocal renderer, poll_id, valid
        if renderer is not None:
            return
        valid = False
        picture.clear()
        refresh.set_sensitive(False)
        status.set_text("正在编译并渲染当前源码…")
        renderer = Renderer()
        poll_id = GLib.timeout_add(100, finish)

    def key(_window, event):
        if event.keyval == Gdk.KEY_Escape:
            window.destroy()
            return True
        if event.keyval == Gdk.KEY_F5:
            start()
            return True
        return False

    selector.connect("changed", show_page)
    refresh.connect("clicked", start)
    leave.connect("clicked", lambda _: window.destroy())
    window.connect("key-press-event", key)
    window.connect("destroy", Gtk.main_quit)
    window.show_all()
    window.fullscreen()
    if args.smoke_seconds > 0:
        GLib.timeout_add_seconds(args.smoke_seconds, lambda: (window.destroy(), False)[1])
    try:
        start()
        Gtk.main()
    finally:
        if poll_id is not None:
            GLib.source_remove(poll_id)
        if renderer is not None:
            renderer.close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
