# QB.py
from BH import BH

class QB:
    def __init__(self):
        self.bhs = []
        # Initialize BH objects with cardNum values from 0 to 3 (four objects)
        for i in range(4):
            self.bhs.append(BH(i))

# Example of how to use the QB class:
if __name__ == "__main__":
    quarterback = QB()
    print("QB object created with BH objects:")
    for bh_obj in quarterback.bh_objects:
        print(f"  BH object with cardNumber: {bh_obj.cardNumber}")
        
