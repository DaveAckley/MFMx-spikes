#!/data/ackley/PART4/code/D/blackholeSpikes/venv/bin/python
from mfmx import MFMx
import tomlikey as tomli
import hashlib

class Config:
    def __init__(self, name, path):
        self.name = name
        self.path = path
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
    cfg = Config("test18","/data/ackley/PART4/code/D/blackholeSpikes/spikes/mpmd14/srcs/host/python/EWD/config/ewd16.dtoml")
    import dumper
    dumper.max_depth=10
    dumper.dump(cfg)
    cfg.load()
    cdict = cfg.hash['image']
    im = MFMx.ImageManager()
    keys = []
    for k,v in cdict.items():
        imagecode = v['code']
        path = v['binfile']
        img = im.makeT6Image(k,imagecode,path)
        print("KONG",k,img)
        keys.append(k)
    for k in keys:
        img = im.getT6Image(k)
        print("ZANG",k,img,img.getImageCode(),img.getBinFileSize(),img.getBinFilePath())
    celldict = cfg.hash['cell']
    for k,c in celldict.items():
        size = MFMx.U8C(*c['size'])
        print("cell",k,c,size)
        cell = MFMx.Cell.make(size)
        print("CELL",cell)
        if c.get('fill'):
            imglabel = c['fill']
            img = im.getT6Image(imglabel)
            print("LAB",imglabel,img,img.getImageCode())
            cell.addImage(MFMx.U8C(255,255), img.getImageCode())
        imgs = c.get('image')
        print("IM",imgs)
        if imgs: 
            for x,v in imgs.items():
                y = next(iter(v.keys()))
                val = v[y]
                x = 255 if x == 'all' else int(x)
                y = 255 if y == 'all' else int(y)
                print("IM",x,y,val)
                img = im.getT6Image(val)
                cell.addImage(MFMx.U8C(x,y),img.getImageCode())
        print("CELLDONE",cell)
        print(cell.toString())
        print("NEXT")
    layoutdict = cfg.hash['layout']
    for k,l in layoutdict.items():
        print("LAYS",k,l);
        lay = MFMx.Layout.make(k,4,1)
        print("LAID",lay);
        cards = l.get('card')
        print("CS",cards)
        if cards: 
            for c,v in cards.items():
                c = 255 if c == 'all' else int(c)
                print("CD",x,val)


print("LAST")

        
  
