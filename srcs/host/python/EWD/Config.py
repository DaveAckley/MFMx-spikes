#!/data/ackley/PART4/code/D/blackholeSpikes/venv/bin/python
from mfmx import MFMx
import tomlikey as tomli
import hashlib
import os

class Config:
    def __init__(self, scriptpath, path):
        self.scriptpath = scriptpath
        self.name = os.path.basename(self.scriptpath).removesuffix(".py")
        self.path = ""
        for cdir in (".",os.path.dirname(os.path.abspath(self.scriptpath))):
            confpath = f"{cdir}/{path}"
            if os.path.isfile(confpath):
                self.path = confpath
                break
        if not self.path:
            print(f"No config file found for '{path}'")
            exit(6)
        self.reset()

    def reset(self):
        self.hash = None
        self.rawfile = None
        self.rawfileCS = None
        
    def __str__(self):
        return f"C:{self.name}"

    def __repr__(self):
        return f"[C:{self.name}:{self.path}]"

    def load(self):
        # Read whole file for storage
        with open(self.path,"rb") as file:
            self.rawfile = file.read()
        print("CONFIGLEN =",len(self.rawfile),"FOR",self.path)

        # Save its hash for checking
        h = hashlib.sha256()
        h.update(self.rawfile)
        self.rawfileCS = h.digest()
        
        # Parse it using dtomlib
        self.hash = tomli.loads(self.rawfile.decode())

    def getFileBytes(self):
        return self.rawfile

    def getFileChecksum(self):
        return self.rawfileCS

    def getRequiredSection(self,name):
        assert name in self.hash, f"Unknown section {name}"
        return self.hash[name]
        
    def getOptionalSection(self,name):
        return self.hash.get(name, None)

    def getInitializedSection(self,name,value):
        have = self.getOptionalSection(name)
        if not have:
            self.hash[name] = value
        return self.getRequiredSection(name)

if __name__ == '__main__':
  c = Config("test18","/data/ackley/PART4/code/D/blackholeSpikes/spikes/mpmd14/srcs/host/python/EWD/config/ewd16.dtoml")
  import dumper
  dumper.max_depth=10
  dumper.dump(c)
  c.load()
  dumper.dump(c)
  
  
