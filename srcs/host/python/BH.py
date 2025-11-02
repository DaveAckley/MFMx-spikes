from T6 import T6
from mfmx import MFMx

class BH:
    def __init__(self, cardNumber):
        self.cardNumber = cardNumber
        self.device = MFMx.BlackHole(cardNumber)
        self.t6s = []
        for i in range(140):
            # The tlbi of each T6 corresponds to its index in the t6s array
            self.t6s.append(T6(i))
        
    def __repr__(self):
        return f"<BH:{self.cardNumber}>"
        
                    
