from symmanager import SymManager

if __name__ == "__main__":
    port = 5555

    sym = SymManager()
    sym.start(port)
    sym.loop()
