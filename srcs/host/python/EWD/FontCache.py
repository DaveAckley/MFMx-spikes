from PIL import ImageFont, ImageDraw, Image
import numpy as np
import string

class CachedFont:
    MAX_GLYPHS = 1024
    # Overlay text is COMPOSITED over the picture (False: the old opaque
    # glyph cells, bg color behind every character).  Each glyph first
    # darkens the picture under a halo (its shape grown by HALO_PX) to
    # 1 - HALO_DIM of its brightness, then draws in fg through its own
    # antialiased coverage, so the grid shows everywhere but a thin
    # shadow around the letters and the text stays readable over atoms.
    COMPOSITE = True
    BOX = False                 # True: dim each glyph's whole cell instead
    HALO_PX = 3
    HALO_DIM = 0.85
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
        # COMPOSITE drawing (2026-11-26): per glyph, its coverage (0..1)
        # and a halo (the coverage grown by HALO_PX, so HALO_PX bigger on
        # every side than the cell); see drawCodepoint.
        self.fg = np.array(fg[::-1], np.float32)   # BGR, like the frames
        self.alpha = {}         # fontImg index -> (h, w) float32 coverage
        self.halo = {}          # fontImg index -> (h+2P, w+2P) float32
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
        if idx is None:         # (was 'not idx': index 0 was redefined
            idx = self.defineCodepoint(codepoint)   # on every draw)
        if CachedFont.COMPOSITE:
            self._composite(idx,img,dx,dy)
            return
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

    @staticmethod
    def _blit_box(img, x0, y0, h, w):
        """Clip the box (x0,y0,w,h) to img: -> (image slices, mask
        slices) or None if nothing is left."""
        H, W = img.shape[0], img.shape[1]
        ix0, iy0 = max(x0, 0), max(y0, 0)
        ix1, iy1 = min(x0 + w, W), min(y0 + h, H)
        if ix0 >= ix1 or iy0 >= iy1:
            return None
        return ((slice(iy0, iy1), slice(ix0, ix1)),
                (slice(iy0 - y0, iy1 - y0), slice(ix0 - x0, ix1 - x0)))

    def _composite(self,idx,img,dx,dy):
        """Halo, then the glyph, over img (BGR uint8) at cell (dx,dy).
        Clipped at the image edges; never raises for a box outside."""
        P = CachedFont.HALO_PX
        halo = self.halo[idx]
        if halo is None:        # blank glyph (space): nothing to draw
            return
        b = self._blit_box(img, dx - P, dy - P, halo.shape[0], halo.shape[1])
        if b is not None:
            (iy, ix), (my, mx) = b
            keep = 1.0 - CachedFont.HALO_DIM * halo[my, mx]
            img[iy, ix] = (img[iy, ix] * keep[..., None]).astype(np.uint8)
        a = self.alpha[idx]
        b = self._blit_box(img, dx, dy, a.shape[0], a.shape[1])
        if b is None:
            return
        (iy, ix), (my, mx) = b
        am = a[my, mx][..., None]
        region = img[iy, ix].astype(np.float32)
        img[iy, ix] = (region * (1.0 - am) + self.fg * am + 0.5).astype(np.uint8)

    def _define_masks(self,idx,codepoint):
        """Coverage and halo of one glyph, for _composite."""
        h, w = self.renderBox
        P = CachedFont.HALO_PX
        m = Image.new("L", (w, h), 0)
        ImageDraw.Draw(m).text((0,0), codepoint, fill=255, anchor="la",
                               font=self.PILFont)
        a = np.asarray(m, np.float32) / 255.0
        self.alpha[idx] = a
        # halo = coverage grown by P (max over a (2P+1)^2 square of shifts)
        pad = np.zeros((h + 2*P, w + 2*P), np.float32)
        pad[P:P+h, P:P+w] = a
        halo = pad.copy()
        if CachedFont.BOX:      # the whole cell, no grown shape
            halo[P:P+h, P:P+w] = 1.0
            self.halo[idx] = halo
            return
        for oy in range(-P, P + 1):
            for ox in range(-P, P + 1):
                if oy or ox:
                    halo = np.maximum(halo, np.roll(np.roll(pad, oy, 0), ox, 1))
        self.halo[idx] = halo if a.any() else None

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
        self._define_masks(idx,codepoint)
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
    
