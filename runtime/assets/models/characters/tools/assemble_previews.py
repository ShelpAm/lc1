"""Encode the rendered asset previews; Pillow is only an authoring dependency."""
from pathlib import Path
from PIL import Image,ImageDraw
import shutil

ROOT=Path(__file__).resolve().parents[1]
for name,cycle,step in [('Idle',120,4),('Walk',36,1),('Sprint',20,1)]:
    folder=ROOT/'cache/render_cache'/('villager_'+name.lower())
    frames=[Image.open(folder/('%03d.png'%i)).convert('RGB') for i in range(1,cycle+1,step)]
    palette=frames[0].quantize(colors=192)
    encoded=[im.quantize(palette=palette) for im in frames]
    durations=[round((i+1)*step*100/30)*10-round(i*step*100/30)*10 for i in range(len(frames))]
    encoded[0].save(ROOT/'previews'/('villager_'+name.lower()+'.gif'),save_all=True,append_images=encoded[1:],duration=durations,loop=0,optimize=False,disposal=2)
    frames[0].save(ROOT/'previews'/('villager_'+name.lower()+'.png'))
    if name=='Sprint':
        sheet=Image.new('RGB',(1000,700))
        for i,index in enumerate([0,5,10,15]): sheet.paste(frames[index].resize((500,700)).resize((250,350)),((i%2)*500+125,(i//2)*350))
        sheet.save(ROOT/'previews/villager_sprint_phases.jpg',quality=92)
print('PREVIEWS_READY')

review_dirs=[ROOT/'cache/render_cache'/('villager_sprint_'+view) for view in ['front','side']]
if all((folder/'020.png').exists() for folder in review_dirs):
    review=[]
    for frame in range(1,21):
        canvas=Image.new('RGB',(720,524),(22,24,26)); draw=ImageDraw.Draw(canvas)
        for index,(folder,label) in enumerate(zip(review_dirs,['Front','Side'])):
            canvas.paste(Image.open(folder/('%03d.png'%frame)).convert('RGB'),(index*360,24))
            draw.text((index*360+12,6),label,fill=(235,235,235))
        review.append(canvas)
    palette=review[0].quantize(colors=192)
    encoded=[im.quantize(palette=palette) for im in review]
    durations=[round((i+1)*100/30)*10-round(i*100/30)*10 for i in range(20)]
    encoded[0].save(ROOT/'previews/villager_sprint_review.gif',save_all=True,append_images=encoded[1:],duration=durations,loop=0,optimize=False,disposal=2)
    sheet=Image.new('RGB',(720,1048))
    for i,frame in enumerate([0,3,6,9,12,15,18,19]):
        sheet.paste(review[frame].resize((360,262)),((i%2)*360,(i//2)*262))
    sheet.save(ROOT/'validation/sprint_coordination_sheet.jpg',quality=93)
    print('SPRINT_REVIEW_READY')
