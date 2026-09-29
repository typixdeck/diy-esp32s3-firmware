"""Real loopback HTTP/TLS tests; never opens a hardware port or remote device."""
import http.client
import socket
import importlib.util
import os
from pathlib import Path
import ssl
import sys
import tempfile
import threading
import unittest
from unittest.mock import patch
sys.path.insert(0,str(Path(__file__).parents[1]/"host"))
import share
import pair

class ShareTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory()
        self.root=Path(self.temp.name)
    def tearDown(self):
        self.temp.cleanup()
    def test_regular_bounds_and_symlinks(self):
        (self.root/"a.txt").write_bytes(b"hello")
        (self.root/"alias.txt").symlink_to(self.root/"a.txt")
        (self.root/"huge.bin").write_bytes(b"x"*(share.MAX_FILE+1))
        (self.root/"sub").mkdir()
        self.assertEqual(share.list_shared(self.root),b"a.txt 5\n")
        self.assertEqual(share.read_shared(self.root,"a.txt"),b"hello")
        for name in ("../a.txt","%2e%2e/a.txt","alias.txt","huge.bin","sub","x\n"):
            with self.assertRaises((OSError,ValueError)): share.read_shared(self.root,name)
    def test_pairing_expected_firmware_and_ack(self):
        replies=iter([b"noise\nTD_DIAG v=1 fw=0.4.0 reset=poweron main=7\r\n",b"TDPAIR OK\n"])
        with patch.object(pair.companion,"send"),patch.object(pair.select,"select",return_value=([3],[],[])),patch.object(pair.os,"read",side_effect=lambda *_:next(replies)):
            pair.exchange(3,"BOOT_STATUS",b"BOOT_040")
            pair.exchange(3,"TDPAIR COMMIT",b"TDPAIR OK")
        with patch.object(pair.companion,"send"),patch.object(pair.select,"select",return_value=([3],[],[])),patch.object(pair.os,"read",return_value=b"TDPAIR ERROR\n"):
            with self.assertRaises(RuntimeError):pair.exchange(3,"TDPAIR COMMIT",b"TDPAIR OK")

    def test_capture_wake_restore_even_on_failure(self):
        import subprocess
        calls=[]
        def run(cmd,**kwargs):
            calls.append(cmd)
            return subprocess.CompletedProcess(cmd,0,stdout="DPI-1 off\n")
        with patch.object(share.subprocess,"run",side_effect=run):
            with self.assertRaises(RuntimeError):
                with share.capture_output({}) as output:
                    self.assertEqual(output,"DPI-1")
                    raise RuntimeError("capture failed")
        self.assertEqual(calls,[["/usr/bin/wlopm"],["/usr/bin/wlopm","--on","DPI-1"],["/usr/bin/wlopm","--off","DPI-1"]])

    def test_https_auth_and_capture(self):
        config=self.root/"config"
        share.initialize(config,"127.0.0.1")
        with self.assertRaises(ValueError): share.initialize(config,"127.0.0.1")
        token=(config/"token").read_text()
        self.assertEqual((config/"token").stat().st_mode & 0o777,0o600)
        server_context=ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        server_context.load_cert_chain(config/"cert.pem",config/"key.pem")
        screenshot=bytes(320*240*2)
        server=share.ShareServer(("127.0.0.1",0),self.root,token,server_context,lambda:screenshot)
        thread=threading.Thread(target=server.serve_forever,daemon=True);thread.start()
        client_context=ssl.create_default_context(cafile=str(config/"cert.pem"))
        def get(path,authorized=True):
            c=http.client.HTTPSConnection("127.0.0.1",server.server_port,context=client_context,timeout=3)
            try:
                c.request("GET",path,headers={"Authorization":"Bearer "+token} if authorized else {})
                r=c.getresponse();return r.status,r.read()
            finally:c.close()
        try:
            self.assertEqual(get("/v1/status",False)[0],401)
            with patch.object(share.companion,"telemetry",return_value=(42,43000,1200000)),patch.object(share,"system_info",return_value=("192.0.2.1",1024,2048,32)):
                self.assertEqual(get("/v1/status"),(200,b"TD2 STATUS 42 43000 1200000 192.0.2.1 1024 2048 32\n"))
            self.assertEqual(get("/v1/screen"),(200,screenshot))
            persistent=http.client.HTTPSConnection("127.0.0.1",server.server_port,context=client_context,timeout=3)
            try:
                persistent.request("GET","/v1/files",headers={"Authorization":"Bearer "+token})
                response=persistent.getresponse();self.assertEqual(response.status,200);response.read()
                original_socket=persistent.sock
                # A second client must work while the first keeps its socket open.
                self.assertEqual(get("/v1/screen"),(200,screenshot))
                persistent.request("GET","/v1/screen",headers={"Authorization":"Bearer "+token})
                response=persistent.getresponse();self.assertEqual(response.read(),screenshot)
                self.assertIs(persistent.sock,original_socket)
            finally:persistent.close()
            # An incomplete TLS handshake cannot monopolize the accept loop.
            idle=socket.create_connection(("127.0.0.1",server.server_port),timeout=3)
            try:self.assertEqual(get("/v1/screen"),(200,screenshot))
            finally:idle.close()

            self.assertEqual(get("/v1/files/../config/token")[0],503)
            self.assertEqual(get("/v1/poweroff")[0],404)
            server.capture=lambda: b"invalid"
            self.assertEqual(get("/v1/screen")[0],503)
        finally:server.shutdown();server.server_close();thread.join(3)

if __name__=="__main__":unittest.main()
