import socket


class Server(object):
    def __init__(self):
        self.socket = None
        self.client = None
        self.buffer = ""

    def start(self, port):
        self.buffer = ""
        self.socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.socket.bind(("127.0.0.1", port))
        self.socket.listen(1)
        self.client = self.socket.accept()[0]

    def is_running(self):
        return self.socket is not None

    def receive(self):
        assert self.client

        while True:
            data = self.client.recv(256)

            if data == "":
                self.stop()
                return None

            self.buffer += data
            line = self.buffer.find("\n")
            if line != -1:
                msg = self.buffer[:line]
                self.buffer = self.buffer[line + 1:]
                return msg

    def send(self, ok, args):
        msg = "1" if ok else "0"
        msg += " {}".format(len(args))

        if len(args) > 0:
            msg += " " + " ".join([str(arg) for arg in args])

        msg += "\n"

        self.client.sendall(msg)

    def stop(self):
        if self.client:
            self.client.close()
            self.client = None
        if self.socket:
            self.socket.close()
            self.socket = None
