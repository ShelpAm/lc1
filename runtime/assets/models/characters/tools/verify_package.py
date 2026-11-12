"""Validate all published model paths, skin streams and animation contracts."""
import json,math,struct,hashlib,bisect
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
manifest=json.loads((ROOT/'manifest.json').read_text(encoding='utf-8'))

def load(path):
    raw=path.read_bytes(); magic,version,size=struct.unpack_from('<III',raw)
    assert (magic,version,size)==(0x46546c67,2,len(raw)),path
    length,kind=struct.unpack_from('<II',raw,12)
    doc=json.loads(raw[20:20+length]); size,kind=struct.unpack_from('<II',raw,20+length)
    return doc,raw[28+length:28+length+size]

def stream(doc,binary,index):
    ac=doc['accessors'][index]; view=doc['bufferViews'][ac['bufferView']]
    count={'SCALAR':1,'VEC2':2,'VEC3':3,'VEC4':4,'MAT4':16}[ac['type']]
    fmt='<'+{5120:'b',5121:'B',5122:'h',5123:'H',5125:'I',5126:'f'}[ac['componentType']]*count
    width=struct.calcsize(fmt); start=view.get('byteOffset',0)+ac.get('byteOffset',0); step=view.get('byteStride',width)
    assert start+(ac['count']-1)*step+width<=view.get('byteOffset',0)+view['byteLength']
    return [struct.unpack_from(fmt,binary,start+i*step) for i in range(ac['count'])]

report={}
for role,character in manifest['characters'].items():
    assert (ROOT/character['source']).is_file()
    entry={}
    for variant,key in [('skinned','model'),('static','static_model')]:
        path=ROOT/character[key]; path.resolve().relative_to(ROOT.resolve())
        doc,binary=load(path)
        assert not doc.get('extensionsRequired'),doc.get('extensionsRequired')
        assert not any(b.get('uri') for b in doc.get('buffers',[]))
        assert not any(im.get('uri') for im in doc.get('images',[]))
        names={n.get('name') for n in doc['nodes']}
        assert {'Eyes','RightHandGrip','LC1_Forward'}<=names
        root=doc['nodes'][doc['scenes'][doc.get('scene',0)]['nodes'][0]]
        assert root['name']=='LC1_Forward' and root['rotation']==[0,1,0,0]
        triangles=0
        for mesh in doc['meshes']:
            for primitive in mesh['primitives']:
                assert primitive.get('mode',4)==4
                attrs=primitive['attributes']; positions=stream(doc,binary,attrs['POSITION'])
                assert all(math.isfinite(c) for row in positions for c in row)
                indices=stream(doc,binary,primitive['indices']); triangles+=len(indices)//3
                assert all(0<=i[0]<len(positions) for i in indices)
                if variant=='skinned':
                    assert {'JOINTS_0','WEIGHTS_0'}<=set(attrs)
                    assert 'JOINTS_1' not in attrs
                    assert all(abs(sum(w)-1)<1e-4 and min(w)>=0 for w in stream(doc,binary,attrs['WEIGHTS_0']))
                    assert all(0<=j<53 for js in stream(doc,binary,attrs['JOINTS_0']) for j in js)
                else:
                    assert set(attrs)<={'POSITION','NORMAL','COLOR_0','TEXCOORD_0','TANGENT'}
                    assert 'COLOR_0' in attrs
        if variant=='skinned':
            assert len(doc['skins'])==1 and len(doc['skins'][0]['joints'])==character['skeleton_joint_count']
            animations={a['name']:a for a in doc.get('animations',[])}
            assert set(animations)==set(character['animations'])
            durations={}
            for name,expected in character['animations'].items():
                clip=animations[name]; duration=0; channels=set(); seam=0
                for channel in clip['channels']:
                    target=channel['target']; target_key=(target['node'],target['path'])
                    assert target_key not in channels; channels.add(target_key)
                    assert target['path'] in ['translation','rotation','scale']
                    sampler=clip['samplers'][channel['sampler']]
                    assert sampler.get('interpolation','LINEAR') in ['LINEAR','STEP']
                    times=[v[0] for v in stream(doc,binary,sampler['input'])]; data=stream(doc,binary,sampler['output'])
                    assert abs(times[0])<1e-6 and all(a<b for a,b in zip(times,times[1:]))
                    assert len(times)==len(data) and all(math.isfinite(c) for row in data for c in row)
                    duration=max(duration,times[-1])
                    if target['path']=='rotation':
                        assert all(abs(sum(c*c for c in q)-1)<1e-4 for q in data)
                        error=min(max(abs(a-b) for a,b in zip(data[0],data[-1])),max(abs(a+b) for a,b in zip(data[0],data[-1])))
                    else: error=max(abs(a-b) for a,b in zip(data[0],data[-1]))
                    seam=max(seam,error)
                    if doc['nodes'][target['node']].get('name')=='Root': assert all(max(abs(a-b) for a,b in zip(row,data[0]))<1e-5 for row in data)
                assert abs(duration-expected['duration_s'])<1e-5,(name,duration)
                assert seam<1e-5,(name,seam)
                durations[name]={'duration_s':duration,'loop_error':seam,'channels':len(channels)}
            entry['animations']=durations
        else:
            assert not doc.get('skins') and not doc.get('animations') and not doc.get('materials')
        entry[variant]={'path':character[key],'MiB':round(path.stat().st_size/1048576,3),'triangles':triangles,'sha256':hashlib.sha256(path.read_bytes()).hexdigest(),'verified':True}
    report[role]=entry
for name,path in manifest['preview_files'].items(): assert (ROOT/path).is_file(),path
authoring=json.loads((ROOT/'validation/villager_authoring.json').read_text(encoding='utf-8'))
assert authoring['clips']['Idle']['airborne_frames']==0
assert authoring['clips']['Sprint']['airborne_frames']>=2
assert authoring['posture_head_retraction_m']>.02
(ROOT/'validation/package_validation.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
print('PACKAGE_VERIFIED',json.dumps({role:{'clips':list(data['animations']),'skinned_MiB':data['skinned']['MiB'],'static_MiB':data['static']['MiB']} for role,data in report.items()}))
