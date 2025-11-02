from LogFeeder import LogFeeder

class BHTileView(LogFeeder):
    def __init__(self,type,x,y,BHObj):
        self.BHObj = BHObj
        self.bhnum = self.BHObj.getCardNumber()
        self.t6key = self.BHObj.getT6Key(self.bhnum,x,y)
        label = type + str(self.t6key)[3:] if type else " - "
        destid = f"#tvlogbuf{self.bhnum}"
        super().__init__(label,destid,id=f"bhtv-{self.t6key}",variant=type)

    # def on_click(self) -> None:
    #     pass

    def __repr__(self):
        return super().__repr__()+self.logBuf
