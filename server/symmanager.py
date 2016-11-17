from message import Message
from server import Server

import z3


class Constraint(object):
    def __init__(self, id, size):
        self.id = id
        self.size = size


class SymManager(object):
    def __init__(self):
        self.server = Server()
        self.symmap = [[]]

    def start(self, port):
        self.server.start(port)

    def loop(self):
        while self.server.is_running():
            msg = self.server.receive()
            if msg is not None:
                self.handle_msg(Message.parse(msg))
            else:
                break

    def handle_msg(self, msg):
        response = (False, [])

        if msg.type == "CREATE_CONSTRAINT":
            response = self.handle_create_constraint(msg)

        self.server.send(response[0], response[1])

    def handle_create_constraint(self, msg):
        state = int(msg.args[0])
        size = int(msg.args[1])
        id = len(self.symmap[state])
        self.symmap[state].append(Constraint(id, size))
        return True, []
