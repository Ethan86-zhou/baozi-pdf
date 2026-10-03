from pathlib import Path
from io import BytesIO
import shutil
from reportlab.pdfgen import canvas
from reportlab.pdfbase import pdfmetrics
from reportlab.pdfbase.ttfonts import TTFont
from reportlab.lib.utils import ImageReader
from PIL import Image, ImageDraw
from pypdf import PdfReader, PdfWriter

root = Path(__file__).resolve().parent / 'fixtures'
root.mkdir(parents=True, exist_ok=True)
font = Path('C:/Windows/Fonts/msyh.ttc')
pdfmetrics.registerFont(TTFont('Chinese', str(font), subfontIndex=0))
buffer = BytesIO()
c = canvas.Canvas(buffer, pagesize=(595,842), pageCompression=1)
c.setFillColorRGB(.10,.25,.34)
c.rect(0,724,595,118,fill=1,stroke=0)
c.setFillColorRGB(1,1,1)
c.setFont('Chinese',26)
c.drawString(44,780,'包子PDF · 阅读验收')
c.setFont('Helvetica',12)
c.drawString(44,750,'WINDOWS NATIVE PDF / ACCEPTANCE SAMPLE')
c.setFillColorRGB(.15,.2,.26)
c.setFont('Chinese',14)
for i,text in enumerate(['中文、English、数字与符号：12345 ± μ Ω', '只读浏览 · 页面缩放 · 翻页导航', '技术参数表 / Electrical characteristics']):
    c.drawString(44,677-i*32,text)
rows=[['参数 Parameter','最小 Min','典型 Typ','最大 Max'],['电压 Voltage','3.0 V','3.3 V','3.6 V'],['电流 Current','—','12 mA','20 mA'],['频率 Clock','1 MHz','24 MHz','48 MHz']]
for row,values in enumerate(rows):
    y=548-row*40
    c.setFillColorRGB(*((.89,.94,.96) if row==0 else (.98,.98,.98)))
    c.rect(44,y-10,507,40,fill=1,stroke=0)
    c.setFillColorRGB(.15,.2,.26);c.setFont('Chinese',11)
    for x,txt in zip([53,253,350,449],values):c.drawString(x,y+5,txt)
c.setStrokeColorRGB(.14,.52,.6);c.setLineWidth(2)
points=[(50,160),(125,190),(200,180),(275,265),(350,300),(425,340),(535,360)]
p=c.beginPath();p.moveTo(*points[0])
for point in points[1:]:p.lineTo(*point)
c.drawPath(p)
c.setFont('Helvetica',10);c.drawString(44,40,'PAGE 1 / 4 - PORTRAIT, CHINESE, VECTOR, TABLE')
c.showPage();c.setPageSize((842,595));c.setFont('Chinese',28);c.drawString(45,525,'第 2 页 · 横向页面')
c.setFont('Helvetica',18);c.drawString(45,480,'LANDSCAPE / PAGE NAVIGATION')
for i in range(8):c.setFillColorRGB(.1+i*.07,.3+i*.06,.5);c.rect(45+i*91,145,75,250,fill=1,stroke=0)
c.showPage();c.setPageSize((595,842))
im=Image.new('RGB',(900,1150),'#f9f4e9');d=ImageDraw.Draw(im)
d.text((60,50),'SCANNED IMAGE / PAGE 3',fill='#233e52',font_size=38)
for j in range(14):d.rectangle((60,160+j*52,820,175+j*52),fill=(80+j*6,90+j*6,100+j*6))
c.drawImage(ImageReader(im),40,70,width=515,height=658);c.showPage()
c.setFont('Chinese',26);c.drawString(50,720,'第 4 页 · PDF 自带旋转')
c.setFont('Helvetica',16);c.drawString(50,670,'INTRINSIC PAGE ROTATION 90 DEGREES');c.showPage();c.save()
reader=PdfReader(buffer);writer=PdfWriter()
for page in reader.pages:writer.add_page(page)
writer.pages[3].rotate(90)
writer.write(root/'sample.pdf')
shutil.copyfile(root/'sample.pdf',root/'中文 路径.pdf')
encrypted=PdfWriter();encrypted.append(root/'sample.pdf');encrypted.encrypt('reader-test');encrypted.write(root/'encrypted.pdf')
c=canvas.Canvas(str(root/'large.pdf'),pagesize=(595,842),pageCompression=1)
for i in range(250):
    c.setFont('Chinese',26);c.drawString(48,745,f'大文档翻页测试 · 第 {i+1} 页')
    c.setFont('Helvetica',13);c.drawString(48,700,f'PAGE {i+1} / 250 - NONSEQUENTIAL NAVIGATION')
    for j in range(32):c.drawString(48,660-j*17,f'Register {i:03d}.{j:02d}   0x{(i*32+j):04X}    R/W    0x00')
    c.showPage()
c.save()
(root/'broken.pdf').write_bytes(b'%PDF-1.7\nthis file is intentionally corrupt\n')
print(root)
