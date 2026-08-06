from PIL import Image
p = r'C:\Users\Administrator\AppData\Local\Temp\qoder-computer-use-images\964a9a72\img-1785417448160301400-612793.png'
im = Image.open(p)
print('size', im.size)
# Panel is on right. Find group header row.
# Look at rows 580..1000, sample x=1200..1580 step 20; report whether row seems 'plain' vs card
for y in range(400, 1000, 8):
    row = [im.getpixel((x, y))[:3] for x in range(1180, 1580, 20)]
    # is any pixel != white?
    non_white = sum(1 for r in row if not (r[0]>245 and r[1]>245 and r[2]>245))
    avg = tuple(sum(r[i] for r in row)//len(row) for i in range(3))
    print(y, avg, 'nw', non_white)
