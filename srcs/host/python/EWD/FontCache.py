from PIL import ImageFont, ImageDraw, Image
import numpy as np
import string

class CachedFont:
    MAX_GLYPHS = 1024
    def __init__(self,fontcache,key,aspectRatio=0.6,preload=True):
        self.fc = fontcache
        self.key = key
        path,size,fg,bg = key
        self.PILFont = ImageFont.truetype(path,size)
        self.ascent,self.descent = self.PILFont.getmetrics()
        self.height = self.ascent + self.descent
        self.renderBox = (self.height,round(aspectRatio*self.height))   # (h,w)

        self.PILImage = Image.new("RGB", (self.renderBox[1],self.renderBox[0]), bg)

        self.fontImg = np.zeros((self.renderBox[0],self.renderBox[1]*self.MAX_GLYPHS,3), np.uint8)
        self.codepoints = {}    # unicode point -> fontImg index
        self.firstFreeIndex = 0
        self.errors = 0
        if not preload: return
        for i in range(0,256):
            ch = chr(i)
            if ch in string.printable:
                self.defineCodepoint(""+ch);


    def __repr__(self):
        path,size,fg,bg = self.key
        return f"<CachedFont:{path},{size},{fg},{bg}>"

    def _coordsForIndex(self,idx):
        if idx >= CachedFont.MAX_GLYPHS:
            raise f"No index {idx}: Out of room in font {self.key}"
        return (idx*self.renderBox[1],0)

    def drawCodepoint(self,codepoint,img,dx,dy):
        idx = self.codepoints.get(codepoint,None)
        if not idx:
            idx = self.defineCodepoint(codepoint)
        (x,y) = self._coordsForIndex(idx)
        s = self.renderBox
        glyph = self.fontImg[y:y+s[0], x:x+s[1]]
        if dy+s[0] < img.shape[0] and dx+s[1] < img.shape[1]:
            try:
                img[dy:dy+s[0], dx:dx+s[1]] = glyph
            except ValueError as e:
                self.errors += 1
                import traceback
                sl = traceback.format_exception(e)
                args = f"img[{dy}:{dy+s[0]}, {dx}:{dx+s[1]}] = self.fontImg[{y}:{y+s[0]}, {x}:{x+s[1]}]"
                self.fc.ewd.logkt(f"FC{self.errors}",f"cp='{codepoint}', args={args}\n{''.join(sl)}")
                if self.errors > 100: exit(1)

    def defineCodepoint(self,codepoint):
        idx = self.firstFreeIndex
        self.firstFreeIndex += 1
        (x,y) = self._coordsForIndex(idx)
        d = ImageDraw.Draw(self.PILImage)
        (_,_,fg,bg) = self.key
        self.PILImage.paste(bg,(0,0) + self.PILImage.size)
        d.text((0,0), codepoint, fill=fg, anchor="la", font=self.PILFont)
        ocrgb = np.array(self.PILImage)
        ocbgr = ocrgb[:, :, ::-1].copy()
        self.fontImg[0:ocbgr.shape[0], x:x+ocbgr.shape[1]] = ocbgr
        self.codepoints[codepoint] = idx
        return idx

class FontCache:
    def __init__(self,ewd = None):
        self.ewd = ewd
        self.firstFreeFontCode = 0
        self.fonts = {}

    def getCachedFont(self,fontCode):
        return self.fonts[fontCode]   # or boom

    def getFontCode(self,path,size,fg,bg,preload=True):
        key = (path,size,fg,bg)
        f = self.fonts.get(key)
        if not f:
            f = self.initFont(key,preload)
        return f

    def initFont(self,key,preload):
        fc = self.firstFreeFontCode
        self.firstFreeFontCode += 1
        cf = CachedFont(self,key,.5,preload=preload)
        self.fonts[fc] = cf
        return fc
    
if __name__ == "__main__":
    import cv2
    demoSize=18
    c = CachedFont(("/data/ackley/PART4/code/D/blackholeSpikes/spikes/mpmd10/srcs/host/python/EWD/fonts/JetBrainsMono/ttf/JetBrainsMono-Light.ttf",demoSize,(200,200,0),(0,0,0)),.5)
    cbox = c.renderBox
    testImg = np.zeros((1024,1024,3), np.uint8)
    for i in range(0,256):
        ch = chr(i)
        if not ch in string.printable:
            continue
        lastPrint = i
        c.drawCodepoint(""+ch,testImg,(i%32)*cbox[1],(i//32)*(cbox[0]))
    spec="ÁĂẮẶẰẲẴǍÂẤẬẦẨẪÄẠÀẢĀĄÅÃÆǼĆČÇĈĊÐĎĐÉĔĚÊẾỆỀỂỄËĖẸÈẺĒĘƐẼǴĞǦĜĢĠĦĤÍĬÎÏİỊÌỈĪĮĨĴĶĹĽĻĿŁŃŇŅŊÑÓŎÔỐỘỒỔỖÖỌÒỎƠỚỢỜỞỠŐŌǪØǾÕŒÞŔŘŖŚŠŞŜȘẞƏŦŤŢȚÚŬÛÜỤÙỦƯỨỰỪỬỮŰŪŲŮŨẂŴẄẀÝŶŸỴỲỶȲỸŹŽŻáăâäàāąåãæǽćčçĉċðďđéĕěêëėèēęəğǧĝġħĥiıíĭîïìīįĩjȷĵĸlĺľŀłmnńŉňŋñóŏôöòơőōøǿõœþŕřsśšşŝßſŧťúŭûüùưűūģķļņŗţǫǵșțạảấầẩẫậắằẳẵặẹẻẽếềểễệỉịọỏốồổỗộớờởỡợụủứừửữựỵỷỹųůũẃŵẅẁýŷÿỳzźžż00123456789₀₁₂₃₄₅₆₇₈₉⁰¹²³⁴⁵⁶⁷⁸⁹½¼¾↋↊૪₿¢¤$₫€ƒ₴₽£₮¥≃∵≬⋈∙≔∁≅∐⎪⋎⋄∣∕∤∸⋐⋱∈∊⋮∎⁼≡≍∹∃∇≳∾⥊⟜⎩⎨⎧⋉⎢⎣⎡≲⋯∓≫≪⊸⊎⨀⨅⨆⊼⋂⋃≇⊈⊉⊽⊴≉∌∉≭≯≱≢≮≰⋢⊄⊅+−×÷=≠><≥≤±≈¬~^∞∅∧∨∩∪∫∆∏∑√∂µ∥⎜⎝⎛⎟⎠⎞%‰﹢⁺≺≼∷≟∶⊆⊇⤖⎭⎬⎫⋊⎥⎦⎤⊢≗∘∼⊓⊔⊡⊟⊞⊠⊏⊑⊐⊒⋆≣⊂≻∋⅀⊃⊤⊣∄∴≋∀⋰⊥⊻⊛⊝⊜⊘⊖⊗⊙⊕↑↗→↘↓↙←↖↔↕↝↭↞↠↢↣↥↦↧⇥↩↪↾⇉⇑⇒⇓⇐⇔⇛⇧⇨⌄⌤➔➜➝➞⟵⟶⟷●○◯◔◕◶◌◉◎◦◆◇◈◊■□▪▫◧◨◩◪◫▲▶▼◀△▷▽◁►◄▻◅▴▸▾◂▵▹▿◃⌶⍺⍶⍀⍉⍥⌾⍟⌽⍜⍪⍢⍒⍋⍙⍫⍚⍱⍦⍎⍊⍖⍷⍩⍳⍸⍤⍛⍧⍅⍵⍹⎕⍂⌼⍠⍔⍍⌺⌹⍗⍌⌸⍄⌻⍇⍃⍯⍰⍈⍁⍐⍓⍞⍘⍴⍆⍮⌿⌷⍣⍭⍨⍲⍝⍡⍕⍑⍏⍬⚇⚠⚡✓✕✗✶@&¶§©®™°′″|¦†ℓ‡№℮␣⎋⌃⌞⌟⌝⌜⎊⎉⌂⇪⌫⌦⌨⌥⇟⇞⌘⏎⏻⏼⭘⏽⏾⌅�˳˷𝔸𝔹ℂ𝔻𝔼𝔽𝔾ℍ𝕀𝕁𝕂𝕃𝕄ℕ𝕆ℙℚℝ𝕊𝕋𝕌𝕍𝕎𝕏𝕐ℤ𝕒𝕓𝕔𝕕𝕖𝕗𝕘𝕙𝕚𝕛𝕜𝕝𝕞𝕟𝕠𝕡𝕢𝕣𝕤𝕥𝕦𝕧𝕨𝕩𝕪▁▂▃▄▅▆▇█▀▔▏▎▍▌▋▊▉▐▕▖▗▘▙▚▛▜▝▞▟░▒▓┌└┐┘┼┬┴├┤─│╡╢╖╕╣║╗╝╜╛╞╟╚╔╩╦╠═╬╧╨╤╥╙╘╒╓╫╪━┃┄┅┆┇┈┉┊┋┍┎┏┑┒┓┕┖┗┙┚┛┝┞┟┠┡┢┣┥┦┧┨┩┪┫┭┮┯┰┱┲┳┵┶┷┸┹┺┻┽┾┿╀╁╂╃╄╅╆╇╈╉╊╋╌╍╎╏╭╮╯╰╱╲╳╴╵╶╷╸╹╺╻╼╽╾╿"
    i = lastPrint
    for uc in spec:
        i += 1
        c.drawCodepoint(uc,testImg,(i%32)*cbox[1],(i//32)*(cbox[0]))
    cv2.imwrite(f"/tmp/FontCache-demo-{demoSize}.png",testImg)
    cv2.imshow("zong",testImg)
    cv2.waitKey()
    
