class Message(object):
    @staticmethod
    def parse(msg):
        data = msg.split(" ")
        return Message(data[0], data[1:])

    def __init__(self, type, args):
        self.type = type
        self.args = args

    def __repr__(self):
        return "Msg ({}): {}".format(self.type, " ".join([str(arg) for arg in self.args]))
