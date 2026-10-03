"""Generate the original BaoziPDF vector artwork and Windows icon (Pillow only)."""
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[1] / 'src' / 'assets'
ROOT.mkdir(parents=True, exist_ok=True)
SCALE = 4
svg = []
image = Image.new('RGBA', (256*SCALE, 256*SCALE))
draw = ImageDraw.Draw(image)

def rounded(box, radius, color):
    draw.rounded_rectangle(tuple(v*SCALE for v in box), radius*SCALE, fill=color)
    x,y,r,b = box
    svg.append(f'<rect x="{x}" y="{y}" width="{r-x}" height="{b-y}" rx="{radius}" fill="{color}"/>')

def path(commands, fill=None, stroke=None, width=1):
    points=[]
    position=(0,0)
    data=[]
    for op,values in commands:
        data.append(op+' '.join(map(str,values)))
        if op in ('M','L'):
            position=tuple(values); points.append(position)
        elif op=='C':
            start=position
            for i in range(1,49):
                t=i/48; u=1-t
                points.append(tuple(u**3*start[j]+3*u*u*t*values[j]+3*u*t*t*values[j+2]+t**3*values[j+4] for j in (0,1)))
            position=tuple(values[-2:])
        elif op=='Z': points.append(points[0])
    scaled=[(round(x*SCALE),round(y*SCALE)) for x,y in points]
    if fill: draw.polygon(scaled, fill=fill)
    if stroke:
        draw.line(scaled, fill=stroke, width=width*SCALE, joint='curve')
        radius=width*SCALE/2
        for x,y in (scaled[0],scaled[-1]): draw.ellipse((x-radius,y-radius,x+radius,y+radius),fill=stroke)
    svg.append(f'<path d="{" ".join(data)}" fill="{fill or "none"}" stroke="{stroke or "none"}" stroke-width="{width}" stroke-linecap="round" stroke-linejoin="round"/>')

rounded((8,8,248,248),54,'#12616B')
# A plump steamed bun: gathered crown, rounded shoulders, flat steamed base.
bun=[('M',(124,65)),('C',(110,69,105,81,86,86)),('C',(56,96,37,120,37,148)),('C',(37,179,65,193,123,193)),('C',(181,193,211,178,211,148)),('C',(211,119,190,95,161,85)),('C',(143,79,137,68,124,65)),('Z',())]
path(bun,fill='#FFF3DA')
path([('M',(45,168)),('C',(72,185,177,185,204,167)),('C',(193,187,169,193,123,193)),('C',(82,193,56,186,45,168)),('Z',())],fill='#EBCB91')
for line in [
    [('M',(121,82)),('C',(107,94,100,107,100,123))],
    [('M',(130,82)),('C',(143,94,149,107,149,123))],
    [('M',(104,88)),('C',(89,98,78,108,75,122))],
    [('M',(146,89)),('C',(162,98,173,109,177,122))],
]: path(line,stroke='#C5914A',width=7)
# A compact document label keeps the app recognizable among food icons.
rounded((139,169,231,224),16,'#E9654F')
font=ImageFont.truetype('C:/Windows/Fonts/segoeuib.ttf',27*SCALE)
draw.text((185*SCALE,196*SCALE),'PDF',font=font,fill='white',anchor='mm')
svg.append('<text x="185" y="206" text-anchor="middle" font-family="Segoe UI, sans-serif" font-weight="700" font-size="27" fill="white">PDF</text>')
(ROOT/'baozi.svg').write_text('<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 256 256">\n'+ '\n'.join(svg)+'\n</svg>\n',encoding='utf-8')
preview=image.resize((256,256),Image.Resampling.LANCZOS)
preview.save(ROOT/'baozi.png')
preview.save(ROOT/'baozi.ico',sizes=[(n,n) for n in (16,20,24,32,40,48,64,128,256)])
print('Created SVG, PNG and nine-resolution ICO in', ROOT)
