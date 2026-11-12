"""Small GLB post-processing utility shared by the asset authoring tools."""
import json
import struct

def finish_glb(path):
    raw=path.read_bytes(); length,kind=struct.unpack_from('<II',raw,12)
    doc=json.loads(raw[20:20+length]); binary=bytearray(raw[28+length:]); shifted={}
    for animation in doc.get('animations',[]):
        inputs={s['input'] for s in animation['samplers']}; origins=[]
        for index in inputs:
            ac=doc['accessors'][index]; view=doc['bufferViews'][ac['bufferView']]
            origins.append(struct.unpack_from('<f',binary,view.get('byteOffset',0)+ac.get('byteOffset',0))[0])
        origin=min(origins)
        for index in inputs:
            ac=doc['accessors'][index]; view=doc['bufferViews'][ac['bufferView']]
            start=view.get('byteOffset',0)+ac.get('byteOffset',0); stride=view.get('byteStride',4); values=[]
            for i in range(ac['count']):
                pos=start+i*stride
                if pos not in shifted:
                    struct.pack_into('<f',binary,pos,struct.unpack_from('<f',binary,pos)[0]-origin); shifted[pos]=origin
                else: assert abs(shifted[pos]-origin)<1e-6
                values.append(struct.unpack_from('<f',binary,pos)[0])
            ac['min']=[min(values)]; ac['max']=[max(values)]
    for scene in doc['scenes']:
        if len(scene['nodes'])==1 and doc['nodes'][scene['nodes'][0]].get('name')=='LC1_Forward': continue
        index=len(doc['nodes']); doc['nodes'].append({'name':'LC1_Forward','rotation':[0,1,0,0],'children':scene['nodes']}); scene['nodes']=[index]
    payload=json.dumps(doc,separators=(',',':')).encode(); payload+=b' '*((-len(payload))%4)
    tail=struct.pack('<II',len(binary),0x004e4942)+binary
    path.write_bytes(struct.pack('<III',0x46546c67,2,20+len(payload)+len(tail))+struct.pack('<II',len(payload),kind)+payload+tail)
    return doc
